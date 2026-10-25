#include <tessera/render/cpu_renderer.hpp>
#include "../detail/checks.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace tessera {
namespace {
bool byte_count(CaptureExtent e, std::size_t& bytes) {
    const auto count = std::uint64_t(e.width) * e.height;
    if (!e.width || !e.height || count > max_cpu_pixel_bytes / 4) return false;
    bytes = static_cast<std::size_t>(count) * 4;
    return true;
}
double decode(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double encode(double c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055; }
std::uint8_t quantize(double c) { return static_cast<std::uint8_t>(std::floor(std::clamp(c, 0.0, 1.0) * 255 + 0.5)); }
using Pixel = std::array<double, 4>;
Pixel linear(Color c) { return {decode(c.r), decode(c.g), decode(c.b), c.a}; }
struct Transform { double a=1, b=0, c=0, d=1, tx=0, ty=0; };
Transform compose(Transform t, Affine2D u) {
    return {t.a*u.a+t.c*u.b, t.b*u.a+t.d*u.b, t.a*u.c+t.c*u.d,
            t.b*u.c+t.d*u.d, t.a*u.tx+t.c*u.ty+t.tx, t.b*u.tx+t.d*u.ty+t.ty};
}
struct Bounds { double left, top, right, bottom; };
Bounds intersection(Bounds a, Bounds b) {
    return {std::max(a.left,b.left), std::max(a.top,b.top), std::min(a.right,b.right), std::min(a.bottom,b.bottom)};
}
bool safe(double v) { return std::isfinite(v) && std::abs(v) <= std::numeric_limits<float>::max(); }
bool safe(Transform t) {
    const double det = t.a*t.d-t.b*t.c;
    return safe(t.a) && safe(t.b) && safe(t.c) && safe(t.d) && safe(t.tx) && safe(t.ty) &&
           std::isfinite(det) && det != 0 && safe(t.d/det) && safe(t.c/det) && safe(t.b/det) && safe(t.a/det);
}
Bounds bounds(Rect r, Transform t, double scale) {
    Bounds result{INFINITY,INFINITY,-INFINITY,-INFINITY};
    for (double x : {double(r.origin.x), double(r.origin.x)+r.size.width})
        for (double y : {double(r.origin.y), double(r.origin.y)+r.size.height}) {
            const double px=(t.a*x+t.c*y+t.tx)*scale, py=(t.b*x+t.d*y+t.ty)*scale;
            result.left=std::min(result.left,px); result.right=std::max(result.right,px);
            result.top=std::min(result.top,py); result.bottom=std::max(result.bottom,py);
        }
    return result;
}
bool safe(Rect r, Transform t, double scale) {
    if (!safe(double(r.origin.x)+r.size.width) || !safe(double(r.origin.y)+r.size.height)) return false;
    auto b=bounds(r,t,scale);
    return safe(b.left) && safe(b.top) && safe(b.right) && safe(b.bottom);
}
bool rounded(double x, double y, double w, double h, double radius) {
    const double r=std::min(radius,std::min(w,h)*0.5);
    const double qx=std::abs(x-w*0.5)-(w*0.5-r), qy=std::abs(y-h*0.5)-(h*0.5-r);
    return std::hypot(std::max(qx,0.0),std::max(qy,0.0))+std::min(std::max(qx,qy),0.0) <= r;
}
bool outer_contains(double x, double y, double w, double h, Transform t) {
    if (x<0 || y<0 || x>w || y>h) return false;
    const double det=t.a*t.d-t.b*t.c;
    // Raster top/left rule in framebuffer space, including mirrored/rotated quads.
    auto included=[](double nx, double ny) { return nx<0 || (nx==0 && ny<0); };
    if (x==0 && !included(-t.d/det,t.c/det)) return false;
    if (x==w && !included(t.d/det,-t.c/det)) return false;
    if (y==0 && !included(t.b/det,-t.a/det)) return false;
    if (y==h && !included(-t.b/det,t.a/det)) return false;
    return true;
}
// Source-over in linear light; a premultiplied source already carries its alpha in RGB.
void blend(std::uint8_t* dst, Pixel src, bool premultiplied=false) {
    const double weight=premultiplied ? 1 : src[3];
    for (unsigned c=0;c<3;++c) dst[c]=quantize(encode(src[c]*weight+decode(dst[c]/255.0)*(1-src[3])));
    dst[3]=quantize(src[3]+dst[3]/255.0*(1-src[3]));
}
template<class Image> Pixel sample(const Image& image, double u, double v) {
    // Clamp normalized coordinates before conversion to avoid unsafe integer casts.
    double x=std::clamp(u,0.0,1.0)*image.extent.width-0.5, y=std::clamp(v,0.0,1.0)*image.extent.height-0.5;
    const auto ix=static_cast<std::int64_t>(std::floor(x)), iy=static_cast<std::int64_t>(std::floor(y));
    const double fx=x-std::floor(x), fy=y-std::floor(y);
    Pixel p{};
    for (unsigned dy=0;dy<2;++dy) for (unsigned dx=0;dx<2;++dx) {
        auto sx=std::clamp<std::int64_t>(ix+dx,0,image.extent.width-1);
        auto sy=std::clamp<std::int64_t>(iy+dy,0,image.extent.height-1);
        const auto index=(std::size_t(sy)*image.extent.width+std::size_t(sx))*4;
        double weight=(dx?fx:1-fx)*(dy?fy:1-fy);
        for (unsigned c=0;c<4;++c) {
            double value=image.pixels[index+c]/255.0;
            if (c<3 && image.encoding==CpuImageEncoding::rgba8_srgb) value=decode(value);
            p[c]+=value*weight;
        }
    }
    return p;
}
struct State { Transform transform; Bounds clip; };
template<class F> struct ScopeExit { F fn; ~ScopeExit() { fn(); } };
std::vector<Diagnostic> reentrant_capture() {
    return {{"reentrant_capture",Severity::error,"/capture","Finish the active capture before entering or changing image bindings.",{}}};
}
} // namespace

std::vector<Diagnostic> CpuRenderer::bind_image(ImageHandle handle, CpuImage image) {
    std::vector<Diagnostic> errors; detail::Checker check(errors);
    if (!handle.value) check.error("invalid_handle","/image","Use a nonzero image handle.");
    std::size_t bytes=0;
    if (!byte_count(image.extent,bytes) || image.pixels.size()!=bytes)
        check.error("invalid_image","/image/pixels","Supply a positive RGBA extent and exact byte count within 256 MiB.");
    check.enumeration(image.encoding,CpuImageEncoding::rgba8_unorm,"/image/encoding");
    check.enumeration(image.alpha,ImageAlpha::premultiplied,"/image/alpha");
    auto it=images_.find(handle.value);
    if (it!=images_.end() && it->second.last_frame>retired_)
        check.error("resource_in_use","/image","Retire the last referencing frame before replacing this image.");
    if (it==images_.end() && images_.size()>=max_cpu_images)
        check.error("image_limit","/image","At most 64 images may be bound.");
    std::size_t total=bytes;
    for (const auto& [id, bound] : images_) if (id!=handle.value) {
        if (bound.pixels.size()>max_cpu_pixel_bytes-std::min(total,max_cpu_pixel_bytes)) { total=max_cpu_pixel_bytes+1; break; }
        total+=bound.pixels.size();
    }
    if (total>max_cpu_pixel_bytes) check.error("image_limit","/image/pixels","Total bound image bytes must fit 256 MiB.");
    if (errors.empty()) images_.insert_or_assign(handle.value,Image{image.extent,{image.pixels.begin(),image.pixels.end()},image.encoding,image.alpha,0});
    return errors;
}
std::vector<Diagnostic> CpuRenderer::unbind_image(ImageHandle handle) {
    auto it=images_.find(handle.value);
    if (it==images_.end()) return {{"invalid_handle",Severity::error,"/image","Bind this handle before removing it.",{}}};
    if (it->second.last_frame>retired_) return {{"resource_in_use",Severity::error,"/image","Retire the last referencing frame before removing this image.",{}}};
    images_.erase(it); return {};
}
void CpuRenderer::retire(std::uint64_t frame) { retired_=std::max(retired_,std::min(frame,submitted_)); }

std::vector<Diagnostic> CpuRenderer::submit(const FrameInfo& frame, const UiDrawList& list) {
    auto errors=validate(frame); auto draw_errors=validate(list);
    errors.insert(errors.end(),draw_errors.begin(),draw_errors.end());
    detail::Checker check(errors);
    auto extent=capture_extent(frame.logical_size,frame.device_scale);
    errors.insert(errors.end(),extent.diagnostics.begin(),extent.diagnostics.end());
    std::size_t bytes=0;
    if (!byte_count(target_.extent,bytes) || target_.pixels.size()!=bytes)
        check.error("invalid_target","/target","Set a positive RGBA target with exact byte count within 256 MiB.");
    if (extent && *extent.value!=target_.extent) check.error("extent_mismatch","/target/extent","Target must equal ceil(logical size * scale).");
    if (frame.frame<=submitted_) check.error("stale_frame","/frame","Use a frame number greater than the last successful submission.");
    if (!errors.empty()) return errors;
    State state{{},{0,0,double(target_.extent.width),double(target_.extent.height)}};
    std::vector<State> stack;
    // Compile one settled state per command; all geometry/resources checked before pixels change.
    std::vector<State> states; states.reserve(list.commands.size());
    for (std::size_t i=0;i<list.commands.size();++i) {
        const auto path="/commands/"+std::to_string(i);
        std::visit([&](const auto& cmd) {
            using T=std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<T,PushTransform>) {
                stack.push_back(state); state.transform=compose(state.transform,cmd.transform);
                if (!safe(state.transform)) check.error("geometry_overflow",path+"/transform","Composed transform must be finite and invertible within float range.");
            } else if constexpr (std::is_same_v<T,PushClip>) {
                stack.push_back(state);
                if (!safe(cmd.rect,state.transform,frame.device_scale)) check.error("geometry_overflow",path+"/rect","Transformed rectangle exceeds safe numeric bounds.");
                state.clip=intersection(state.clip,bounds(cmd.rect,state.transform,frame.device_scale));
            } else if constexpr (std::is_same_v<T,PopTransform> || std::is_same_v<T,PopClip>) {
                state=stack.back(); stack.pop_back();
            } else if constexpr (std::is_same_v<T,DrawGlyphRun>) {
                check.error("unsupported_command",path,"Prepare glyph atlas images before submission, or use CpuFrameCapture with a GlyphCache.");
            } else {
                if (!safe(cmd.rect,state.transform,frame.device_scale)) check.error("geometry_overflow",path+"/rect","Transformed rectangle exceeds safe numeric bounds.");
                if constexpr (std::is_same_v<T,DrawImage>) {
                    if (!images_.contains(cmd.image.value)) check.error("invalid_handle",path+"/image","Bind the referenced image before submission.");
                    if (cmd.source.origin.x<0 || cmd.source.origin.y<0 || double(cmd.source.origin.x)+cmd.source.size.width>1 || double(cmd.source.origin.y)+cmd.source.size.height>1)
                        check.error("out_of_range",path+"/source","Source rectangle must fit normalized [0,1] coordinates.");
                }
            }
        },list.commands[i]);
        states.push_back(state);
    }
    if (!errors.empty()) return errors;
    for (std::size_t i=0;i<list.commands.size();++i) {
        const auto& s=states[i];
        std::visit([&](const auto& cmd) {
            using T=std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<T,DrawRect> || std::is_same_v<T,DrawBorder> || std::is_same_v<T,DrawImage>) {
                auto box=intersection(s.clip,bounds(cmd.rect,s.transform,frame.device_scale));
                if (box.right<=box.left || box.bottom<=box.top || cmd.rect.size.width==0 || cmd.rect.size.height==0) return;
                auto edge=[](double e, std::uint32_t limit) { return static_cast<std::uint32_t>(std::clamp(std::ceil(e-0.5),0.0,double(limit))); };
                const auto x0=edge(box.left,target_.extent.width), x1=edge(box.right,target_.extent.width);
                const auto y0=edge(box.top,target_.extent.height), y1=edge(box.bottom,target_.extent.height);
                const auto& t=s.transform; const double det=t.a*t.d-t.b*t.c;
                for (auto y=y0;y<y1;++y) for (auto x=x0;x<x1;++x) {
                    const double px=(x+0.5)/frame.device_scale-t.tx, py=(y+0.5)/frame.device_scale-t.ty;
                    const double lx=(t.d*px-t.c*py)/det-cmd.rect.origin.x, ly=(-t.b*px+t.a*py)/det-cmd.rect.origin.y;
                    if (!outer_contains(lx,ly,cmd.rect.size.width,cmd.rect.size.height,t)) continue;
                    Pixel color;
                    bool premultiplied=false;
                    if constexpr (std::is_same_v<T,DrawImage>) {
                        const auto& bound=images_.at(cmd.image.value);
                        color=sample(bound,cmd.source.origin.x+lx/cmd.rect.size.width*cmd.source.size.width,
                            cmd.source.origin.y+ly/cmd.rect.size.height*cmd.source.size.height);
                        auto tint=linear(cmd.tint); for (unsigned c=0;c<4;++c) color[c]*=tint[c];
                        // The straight tint's alpha scales premultiplied RGB as well.
                        premultiplied=bound.alpha==ImageAlpha::premultiplied;
                        if (premultiplied) for (unsigned c=0;c<3;++c) color[c]*=tint[3];
                    } else {
                        if (!rounded(lx,ly,cmd.rect.size.width,cmd.rect.size.height,cmd.corner_radius)) continue;
                        if constexpr (std::is_same_v<T,DrawBorder>) {
                            const auto& w=cmd.widths;
                            const double iw=double(cmd.rect.size.width)-w.left-w.right, ih=double(cmd.rect.size.height)-w.top-w.bottom;
                            const double radius=std::max(0.0,double(cmd.corner_radius)-std::max({w.top,w.right,w.bottom,w.left}));
                            const double ix=lx-w.left, iy=ly-w.top;
                            if (iw>0 && ih>0 && ix>=0 && iy>=0 && ix<iw && iy<ih && rounded(ix,iy,iw,ih,radius)) continue;
                        }
                        color=linear(cmd.color);
                    }
                    blend(target_.pixels.data()+(std::size_t(y)*target_.extent.width+x)*4,color,premultiplied);
                }
            }
        },list.commands[i]);
    }
    for (const auto& command:list.commands) if (auto image=std::get_if<DrawImage>(&command)) images_.at(image->image.value).last_frame=frame.frame;
    submitted_=frame.frame; return {};
}

std::vector<Diagnostic> CpuFrameCapture::bind_image(ImageHandle handle, CpuImage image) {
    return capturing_ ? reentrant_capture() : renderer_.bind_image(handle,image);
}
std::vector<Diagnostic> CpuFrameCapture::unbind_image(ImageHandle handle) {
    return capturing_ ? reentrant_capture() : renderer_.unbind_image(handle);
}
Result<CapturedImage> CpuFrameCapture::capture(const CaptureRequest& request) {
    if (capturing_) return {{},reentrant_capture()};
    capturing_=true;
    ScopeExit entry{[&] { capturing_=false; }};
    auto extent=capture_extent(request.logical_size,request.device_scale);
    if (!extent) return {{},std::move(extent.diagnostics)};
    if (*extent.value!=request.extent) return {{},{{"extent_mismatch",Severity::error,"/extent","Supply the extent computed from logical size and scale.",{}}}};
    if (!request.paint) return {{},{{"missing_input",Severity::error,"/paint","Supply a draw list.",{}}}};
    std::size_t bytes=0;
    if (!byte_count(request.extent,bytes)) return {{},{{"capture_limit",Severity::error,"/extent","CPU target bytes must fit 256 MiB.",{}}}};
    if (renderer_.submitted_==UINT64_MAX) return {{},{{"out_of_range",Severity::error,"/frame","CPU frame numbers are exhausted.",{}}}};
    std::optional<GlyphAtlasFrame> atlas;
    std::vector<Diagnostic> diagnostics;
    const auto* list=request.paint;
    bool has_glyphs=std::any_of(list->commands.begin(),list->commands.end(),[](const auto& c){return std::holds_alternative<DrawGlyphRun>(c);});
    if (has_glyphs && glyphs_) {
        // Find a free consecutive range, including source handles not bound by the host.
        std::uint64_t first=1;
        for (;;) {
            bool collision=false;
            bool exhausted=false;
            auto advance=[&](std::uint64_t id) { if (id>=first && id-first<16) {
                if (id==UINT64_MAX) exhausted=true;
                else first=id+1;
                collision=true;
            } };
            if (first>UINT64_MAX-16) return {{},{{"image_limit",Severity::error,"/images","No atlas handle range is available.",{}}}};
            for (const auto& [id,image]:renderer_.images_) advance(id);
            for (const auto& command:list->commands) if (auto image=std::get_if<DrawImage>(&command)) advance(image->image.value);
            if (exhausted) return {{},{{"image_limit",Severity::error,"/images","No atlas handle range is available.",{}}}};
            if (!collision) break;
        }
        auto prepared=prepare_glyph_atlas(*list,*glyphs_,request.device_scale,{first});
        if (!prepared) return {{},std::move(prepared.diagnostics)};
        diagnostics=std::move(prepared.diagnostics); atlas=std::move(*prepared.value); list=&atlas->draw_list;
    }
    std::vector<ImageHandle> temporary;
    temporary.reserve(16);
    auto release=[&] { for (auto handle:temporary) renderer_.unbind_image(handle); };
    ScopeExit resources{[&] { renderer_.set_target({}); release(); }};
    // All copies precede rendering. Roll back temporary bindings on rejection.
    if (atlas) for (const auto& page:atlas->pages) {
        std::vector<std::uint8_t> pixels(page.coverage.size()*4,255);
        for (std::size_t i=0;i<page.coverage.size();++i) pixels[i*4+3]=page.coverage[i];
        auto errors=renderer_.bind_image(page.image,{{page.size,page.size},pixels,CpuImageEncoding::rgba8_unorm});
        if (!errors.empty()) return {{},std::move(errors)};
        temporary.push_back(page.image);
    }
    CapturedImage image{request.extent,CaptureFormat::rgba8_srgb,std::vector<std::uint8_t>(bytes,0)};
    renderer_.set_target({image.extent,image.pixels});
    const auto frame=renderer_.submitted_+1;
    auto errors=renderer_.submit({frame,request.logical_size,request.device_scale},*list);
    if (!errors.empty()) return {{},std::move(errors)};
    renderer_.retire(frame);
    return {std::move(image),std::move(diagnostics)};
}
} // namespace tessera
