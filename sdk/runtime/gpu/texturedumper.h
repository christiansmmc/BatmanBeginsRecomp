#pragma once
#include <cstdint>
#include <vector>

#include <dxgiformat.h>

#include "gpu/texture.h"

// Texture packs and texture dumps. A console texture is named by a hash of its level 0 bytes, size and format; an
// image in textures\replacement\ beside the exe with that hash in its name is drawn in its place at any resolution,
// and textures\dumps\ gets each texture once as a PNG or DDS file to edit or upscale.
namespace gpu::texturedumper {

// rows tightly packed, block rows for the BC formats
struct Level {
    std::vector<uint8_t> px;
    uint32_t pitch = 0;
};

// level 0 first
struct Image {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<Level> levels;
};

enum class Lookup { None, Loading, Ready };

// The game drawing over some of the console's textures itself, as the key prompts do: a picture made from the console's own
// pixels, as often as it changes. A file of the same key in the texture pack goes first. The functions are called on the
// render thread.
struct Overlay {
    // can a texture of this size be one the game draws over; keeps the bytes of every other from being hashed
    bool (*wants_size)(uint32_t width, uint32_t height);
    // would paint draw over this texture now; the console's pixels are not decoded otherwise
    bool (*paints)(uint64_t key, uint32_t width, uint32_t height);
    // `bgra` is level 0 of the texture with key `key`, B G R A, rows tightly packed. Change it, and the size when the picture
    // is made bigger; false leaves the console's.
    bool (*paint)(uint64_t key, uint32_t& width, uint32_t& height, std::vector<uint8_t>& bgra);
    // a number that changes whenever what paint draws does. A picture is kept with the number it was made at and serves
    // again whenever the number comes back, so switching back and forth paints nothing.
    uint32_t (*revision)();
};
void SetOverlay(const Overlay& overlay);  // once, before the first draw
bool OverlaySize(uint32_t width, uint32_t height);

// the picture paint makes for a texture's bytes, kept for Take; true when there is one
bool MakeOverlay(uint64_t key, const TextureDesc& d, const uint8_t* guest, const uint32_t* palette);

// latches the texture settings, the render thread's, and lists the pack again once its files changed.
// true then: every pack image loads anew, GPU copies made before are stale
bool Update();
// changes with any texture setting: every texture's key and pack image are looked up again
uint32_t Generation();
// changes with the overlay's revision: the textures of a size it draws over look their picture up again
uint32_t OverlayGeneration();
// the keys of the overlay's pictures handed out before its revision last changed: their GPU copies are stale. The pack's
// own images are not among them, since a pack image is only loaded once.
std::vector<uint64_t> StaleOverlays();
// replacement on and the pack holds images
bool Replacing();

// the name of a texture's bytes, a P8 texture's with its 256 colours; 0 when neither the pack nor dumping applies
uint64_t Key(const TextureDesc& d, const FormatInfo& fi, const uint8_t* guest, const uint32_t* palette);

// once per key while dumping: PNG level 0, or DDS with every level in the format the renderer uploads
void Dump(uint64_t key, const TextureDesc& d, const uint8_t* guest, const uint32_t* palette);

// Ready moves the image into out (an overlay's picture is copied, and kept); the first ask starts loading it off the calling
// thread. None: replacement off, no pack image or current overlay picture for key, or its file did not load
Lookup Take(uint64_t key, Image& out);

}  // namespace gpu::texturedumper
