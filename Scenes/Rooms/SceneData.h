#ifndef SLIPPI_ROOMS_SCENE_DATA_H
#define SLIPPI_ROOMS_SCENE_DATA_H

#include "../../m-ex/MexTK/mex.h"

// Shared by the rooms list and room scenes. This struct is defined in asm via directives
// and has no padding, so we need to use pack(1)
#pragma pack(1)
typedef struct Rooms_SceneData {
  u8 enter_room;
  u8 visibility;
  u8 mode;
  u8 capacity;  // 0 is no limit
  u8 stage_mode;
  u8 last_char;  // Local player's last pick, kept between rooms
  u8 last_color;
} Rooms_SceneData;
#pragma pack()

#endif  // SLIPPI_ROOMS_SCENE_DATA_H
