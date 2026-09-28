#ifndef SLIPPI_ROOM_H
#define SLIPPI_ROOM_H

#include "../../Common.h"
#include "../../Components/CharPickerDialog.h"
#include "../../Components/CharStageBoxSelector.h"
#include "../../ExiSlippi.h"
#include "../../Files.h"
#include "../../m-ex/MexTK/mex.h"
#include "SceneData.h"

// Frame of the panels animation where the panels are fully open
#define PANELS_OPEN_FRAME 150

// Panel joint indices, depth first. Moving the frame's corner joints resizes the inner
// frame without stretching its corners
#define PANELS_MIDDLE_JOINT 1
#define PANELS_FRAME_TOP_RIGHT 6
#define PANELS_FRAME_BOTTOM_LEFT 7
#define PANELS_FRAME_TOP_LEFT 8
#define PANELS_FRAME_BOTTOM_RIGHT 9
#define PANELS_TOP_JOINT 10
#define PANELS_BOTTOM_JOINT 15

// The middle panel is split into a stage frame and a list frame
#define STAGE_LEFT -25
#define STAGE_RIGHT 6.5
#define STAGE_CENTER_X ((STAGE_LEFT + STAGE_RIGHT) / 2)
#define LIST_LEFT 8.5
#define LIST_RIGHT 25

#define STAGE_BOX_LEFT_X -15.5
#define STAGE_BOX_RIGHT_X -3
#define STAGE_BOX_Y 2.5
#define STRIKE_BOX_GAP 5.2

#define LIST_LINES 9
#define LIST_LINE_GAP 250
#define PROMPT_GAP_X 1800

#define ROOM_MAX_MEMBERS 32
#define STAGE_COUNT 6

#define SET_DELAY_FRAMES 120
#define CROWN_DELAY_FRAMES 240
#define TURN_DELAY_FRAMES 60  // Used by test players
#define HOLD_FRAMES 60

#define TURN_SECONDS 30
#define GRACE_SECONDS 3
#define WARN_SECONDS 10
#define PANIC_SECONDS 3

typedef enum Room_Side {
  Room_Side_NONE = -1,
  Room_Side_WINNER,
  Room_Side_CHALLENGER,
} Room_Side;

typedef enum Room_Phase {
  Room_Phase_WAITING,
  Room_Phase_STRIKING,
  Room_Phase_CHOOSING,
  Room_Phase_PICKING,
  Room_Phase_PLAYING,
} Room_Phase;

typedef struct Room_Member {
  char name[31];
  char connect_code[10];
  u8 char_id;
  u8 char_color;
  u8 crowns;
} Room_Member;

// Room state shared with every member. The host's copy is the source of truth
typedef struct Room_State {
  Room_Member members[ROOM_MAX_MEMBERS];
  u8 member_count;
  u8 queue[ROOM_MAX_MEMBERS];
  u8 queue_count;
  s8 sides[2];  // Member index on each side, -1 if empty
  u8 streak;
  u32 beaten;  // Bit per member the current winner has beaten
  s8 crowned;  // Member who just earned a crown, -1 if none
  Room_Phase phase;
  u8 struck[STAGE_COUNT];
  CSIcon_Material stage;
  u8 has_picked[2];
  u8 play_char[2];  // Character each side plays as, with random resolved
  u8 play_color[2];
} Room_State;

typedef struct Room_Data {
  Rooms_SceneData *scene_data;
  ExiSlippi_GetOnlineStatus_Response *online_status;
  Room_State state;
  Text *text;
  Text *left_text;
  Text *right_text;
  Text *list_text;
  int name_subtext_ids[2];
  int code_subtext_ids[2];
  int timer_subtext_id;
  int streak_subtext_id;
  int vs_subtext_id;
  int status_subtext_id;
  int count_subtext_id;
  int prompt_subtext_ids[3];
  int timer_frames;
  int hold_idx;  // Prompt being held, -1 if none
  int hold_frames;
  CSBoxSelector *char_selectors[2];
  CSBoxSelector *stage_selector;
  CSBoxSelector *stage_strike_selectors[STAGE_COUNT];
  CharPickerDialog *char_picker_dialog;
  int selector_idx;
  u8 prev_picker_char;
  u16 code;
  u16 password;
  u8 should_exit;
} Room_Data;

void LoadOnlineStatus();
JOBJ *LoadPanels();
JOBJ *GetJoint(JOBJ *root, int index);
void HideJoint(JOBJ *jobj);
void SetFrameSides(JOBJ *panels, float left, float right);
Text *CreateText(u8 align);
int AddSubtext(Text *text, float x, float y, float scale, char *str);
void InitHeader();
void InitStage();
void InitList();
void InitPrompts();
int AddMember(char *name, char *connect_code);
int QueuePos(int member);
u8 IsOnSide(int member);
void JoinQueue(int member);
void LeaveQueue(int member);
void FillSides();
void StartSet();
void FinishSet(Room_Side winner);
u8 HasBeatenEveryone();
void GetDisplayName(char *out, int member);
Room_Side TurnSide();
void Strike(int idx);
void Choose(int idx);
void Pick(Room_Side side, u8 char_id, u8 char_color);
void SetColor(int member, u8 char_color);
void StartMatch();
u8 UpdateTimer();
void TimeOut(Room_Side side);
void OnTurnChange();
void OnQueueChange();
void UpdateStage();
void UpdateList();
u8 GetVanilaMaxColors(u8 charId);
u8 GetNextColor(u8 char_id, u8 color_id, int incr);
void OnCharSelectionComplete(CharPickerDialog *cpd, u8 is_selection);
u8 CanChangeColor();
void HandleColorInputs(u64 downInputs);
void HandleHoldInputs(u64 heldInputs);
void HandleStageInputs(u64 downInputs, u64 scrollInputs);
void CObjThink(GOBJ *gobj);
void InputsThink(GOBJ *gobj);

#endif  // SLIPPI_ROOM_H
