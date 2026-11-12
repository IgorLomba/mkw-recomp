# runtime/third_party

| Folder | What it is |
|---|---|
| `imgui/` | Dear ImGui 1.91.9 (MIT) with its back ends: Vulkan (in-game menu), SDL3 and SDL_Renderer (start screen) |
| `nxformat/` | `nx_reader.hpp` from [NXFormat](https://github.com/fab918/nxformat) @ 208b815 (MIT, its `LICENSE` beside it), unchanged: the reader of `.nx` dumps, called from `sdk/fs/nx.cpp` |
| `bc7enc/` | `bc7enc.cpp` from [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo) @ b9438627 (MIT), unchanged; `bc7enc_c.cpp` calls it from C (`vk_texpre.c`) |
