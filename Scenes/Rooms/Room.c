#include "Room.h"

#include "../../Components/CharStageIcon.h"
#include "../../Components/StockIcon.h"
#include "../../Game/Sounds.h"

static HSD_Archive *gui_archive;
static GUI_GameSetup *gui_assets;
static Room_Data *data;

static char *mode_labels[] = {"Singles", "Doubles", "Ironman", "Crews", "Tourney"};
static char *stage_labels[] = {"Random stages", "Stage draft"};

// Starter stages, used for both the draft and random stages. Same order as Dolphin's room
static CSIcon_Material stages[ROOM_STAGE_COUNT] = {
    CSIcon_Material_Fountain,
    CSIcon_Material_Dreamland,
    CSIcon_Material_Yoshis,
    CSIcon_Material_FinalDestination,
    CSIcon_Material_Battlefield,
    CSIcon_Material_Pokemon,
};
static char *stage_names[ROOM_STAGE_COUNT] = {
    "Fountain of Dreams",
    "Dreamland",
    "Yoshi's Story",
    "Final Destination",
    "Battlefield",
    "Pokemon Stadium",
};

// Indexed by ExiSlippi_RoomError
static char *join_errors[] = {
    "",
    "Rooms are unavailable right now",
    "No room with that code",
    "Wrong password",
    "That room is full",
    "Too many tries, wait a few minutes",
    "Could not connect to the room",
    "Lost connection to the room",
    "The room did not let you in",
};

const GXColor text_color = {255, 255, 255, 255};
const GXColor dim_color = {128, 128, 128, 255};
const GXColor warn_color = {254, 202, 52, 255};
const GXColor panic_color = {255, 50, 50, 255};

void minor_load(Rooms_SceneData *minor_data) {
  data = calloc(sizeof(Room_Data));
  data->scene_data = minor_data;
  data->hold_idx = -1;

  // Allocate some memory used throughout
  data->state_query = calloc(sizeof(ExiSlippi_GetRoomState_Query));
  data->state = calloc(sizeof(ExiSlippi_GetRoomState_Response));
  data->prev_state = calloc(sizeof(ExiSlippi_GetRoomState_Response));
  data->action_query = calloc(sizeof(ExiSlippi_RoomAction_Query));
  data->match_state = ExiSlippi_LoadMatchState(0);

  // Coming back from the room's match
  if (minor_data->start_match) {
    minor_data->start_match = false;
    SendAction(ExiSlippi_RoomAction_MATCH_ENDED, 0, 0);
  }

  // Set up input handler. Initialize at top to make sure it runs before anything else
  GOBJ *input_handler_gobj = GObj_Create(4, 0, 128);
  GObj_AddProc(input_handler_gobj, InputsThink, 0);

  // Load file
  gui_archive = Archive_LoadFile("GameSetup_gui.dat");
  gui_assets = Archive_GetPublicAddress(gui_archive, "ScGamTour_scene_data");

  GOBJ *cam_gobj = GObj_Create(2, 3, 128);
  COBJ *cam_cobj = COBJ_LoadDesc(gui_assets->cobjs[0]);
  GObj_AddObject(cam_gobj, 1, cam_cobj);
  GOBJ_InitCamera(cam_gobj, CObjThink, 0);
  GObj_AddProc(cam_gobj, MainMenu_CamRotateThink, 5);

  // Indicates which gx_links to render
  cam_gobj->cobj_links = (1 << 0) + (1 << 1) + (1 << 2) + (1 << 3) + (1 << 4);

  // store cobj to static pointer, needed for MainMenu_CamRotateThink
  void **stc_cam_cobj = (R13 + (-0x4ADC));
  *stc_cam_cobj = gui_assets->cobjs[0];

  // create fog
  GOBJ *fog_gobj = GObj_Create(14, 2, 0);
  HSD_Fog *fog = Fog_LoadDesc(gui_assets->fog[0]);
  GObj_AddObject(fog_gobj, 4, fog);
  GObj_AddGXLink(fog_gobj, GXLink_Fog, 0, 128);

  // create lights
  GOBJ *light_gobj = GObj_Create(3, 4, 128);
  LOBJ *lobj = LObj_LoadAll(gui_assets->lights);
  GObj_AddObject(light_gobj, 2, lobj);
  GObj_AddGXLink(light_gobj, GXLink_LObj, 0, 128);

  // create background
  JOBJ_LoadSet(0, gui_assets->jobjs[GUI_GameSetup_JOBJ_Background], 0, 0, 3, 1, 1, GObj_Anim);

  // Load panels, narrowing the middle frame to fit the stage area
  JOBJ *panels = LoadPanels();
  SetFrameSides(panels, STAGE_LEFT, STAGE_RIGHT);

  // Load a second copy of the panels and only show its frame, used for the list
  JOBJ *list_panels = LoadPanels();
  HideJoint(GetJoint(list_panels, PANELS_MIDDLE_JOINT));
  HideJoint(GetJoint(list_panels, PANELS_TOP_JOINT));
  HideJoint(GetJoint(list_panels, PANELS_BOTTOM_JOINT));
  SetFrameSides(list_panels, LIST_LEFT, LIST_RIGHT);

  // Prepare text
  data->text = CreateText(1);
  data->left_text = CreateText(0);
  data->right_text = CreateText(2);
  data->list_text = CreateText(0);

  InitHeader();
  InitStage();
  InitList();
  InitPrompts();

  // Initialize dialog last to make sure it's on top of everything
  data->char_picker_dialog = CharPickerDialog_Init(gui_assets, OnCharSelectionComplete, GetNextColor);
  CharPickerDialog_SetPos(data->char_picker_dialog, (Vec3){0, -9, 0});

  FetchState();
  OnStateChange();
}

void minor_think() {
  if (data->should_exit) {
    Scene_ExitMinor();
    return;
  }

  FetchState();

  // The room is gone, such as after leaving it
  if (!data->state->is_active) {
    data->should_exit = true;
    return;
  }

  if (memcmp(data->state, data->prev_state, sizeof(ExiSlippi_GetRoomState_Response)) != 0) {
    OnStateChange();
  }

  if (IsInRoom()) {
    UpdateCharPicker();
    HandleMatchHandoff();
  }
}

void minor_exit(Rooms_SceneData *minor_data) {
}

// Loads a copy of the panels, frozen fully open
JOBJ *LoadPanels() {
  GOBJ *gobj = JOBJ_LoadSet(0, gui_assets->jobjs[GUI_GameSetup_JOBJ_Panels], 0, 0, 3, 1, 1, GObj_Anim);
  GObj_RemoveProc(gobj);

  JOBJ *jobj = gobj->hsd_object;
  JOBJ_ReqAnimAll(jobj, PANELS_OPEN_FRAME);
  JOBJ_AnimAll(jobj);
  return jobj;
}

// Index is depth first, the same order HSDRawViewer lists joints in
JOBJ *GetJoint(JOBJ *root, int index) {
  JOBJ *jobj = 0;
  JOBJ_GetChild(root, &jobj, index, -1);
  return jobj;
}

void HideJoint(JOBJ *jobj) {
  for (DOBJ *d = jobj->dobj; d; d = d->next) {
    d->flags |= DOBJ_HIDDEN;
  }
}

// Moves the left and right edges of the inner frame
void SetFrameSides(JOBJ *panels, float left, float right) {
  int left_joints[] = {PANELS_FRAME_TOP_LEFT, PANELS_FRAME_BOTTOM_LEFT};
  int right_joints[] = {PANELS_FRAME_TOP_RIGHT, PANELS_FRAME_BOTTOM_RIGHT};

  for (int i = 0; i < 2; i++) {
    JOBJ *left_jobj = GetJoint(panels, left_joints[i]);
    left_jobj->trans.X = left;
    JOBJ_SetMtxDirtySub(left_jobj);

    JOBJ *right_jobj = GetJoint(panels, right_joints[i]);
    right_jobj->trans.X = right;
    JOBJ_SetMtxDirtySub(right_jobj);
  }
}

// Same text setup as ranked. Subtext positions are 100 units per world unit, with y
// pointing down
Text *CreateText(u8 align) {
  Text *text = Text_CreateText(0, 0);
  text->kerning = 1;
  text->align = align;
  text->use_aspect = 1;
  text->scale = (Vec2){0.01, 0.01};

  // Widen the text so long lines don't get squeezed
  text->aspect.X *= 5;
  return text;
}

int AddSubtext(Text *text, float x, float y, float scale, char *str) {
  int id = Text_AddSubtext(text, x, y, str);
  Text_SetScale(text, id, scale, scale);
  return id;
}

// Room code and password on the left, room settings on the right
void InitHeader() {
  data->code_subtext_id = AddSubtext(data->left_text, -2800, -1940, 5, "");
  data->password_subtext_id = AddSubtext(data->left_text, -2800, -1740, 3, "");
  Text_SetColor(data->left_text, data->password_subtext_id, &dim_color);

  data->mode_subtext_id = AddSubtext(data->right_text, 2800, -1940, 5, "");
  data->stage_mode_subtext_id = AddSubtext(data->right_text, 2800, -1740, 3, "");
  Text_SetColor(data->right_text, data->stage_mode_subtext_id, &dim_color);

  // Init timer subtext
  data->timer_subtext_id = AddSubtext(data->text, 0, -1880, 6, "");
}

// Winner on the left, challenger on the right and the stage between them. The strike
// selectors replace all three during the draft
void InitStage() {
  float xs[] = {STAGE_BOX_LEFT_X, STAGE_BOX_RIGHT_X};
  for (int i = 0; i < 2; i++) {
    CSBoxSelector *bs = CSBoxSelector_Init(gui_assets);
    CSBoxSelector_SetPos(bs, (Vec3){xs[i], STAGE_BOX_Y, 0});
    data->char_selectors[i] = bs;

    data->name_subtext_ids[i] = AddSubtext(data->text, xs[i] * 100, -900, 4, "");
    data->code_subtext_ids[i] = AddSubtext(data->text, xs[i] * 100, -720, 3, "");
    Text_SetColor(data->text, data->code_subtext_ids[i], &dim_color);
  }

  // Win streak, under the winner
  data->streak_subtext_id = AddSubtext(data->text, STAGE_BOX_LEFT_X * 100, 130, 3, "");
  Text_SetColor(data->text, data->streak_subtext_id, &warn_color);

  data->stage_selector = CSBoxSelector_Init(gui_assets);
  CSBoxSelector_SetPos(data->stage_selector, (Vec3){STAGE_CENTER_X, STAGE_BOX_Y, 0});

  float x = STAGE_CENTER_X - STRIKE_BOX_GAP * (ROOM_STAGE_COUNT - 1) / 2;
  for (int i = 0; i < ROOM_STAGE_COUNT; i++) {
    CSBoxSelector *bs = CSBoxSelector_Init(gui_assets);
    CSIcon_SetMaterial(bs->icon, stages[i]);
    CSBoxSelector_SetPos(bs, (Vec3){x + STRIKE_BOX_GAP * i, STAGE_BOX_Y, 0});
    data->stage_strike_selectors[i] = bs;
  }

  data->vs_subtext_id = AddSubtext(data->text, STAGE_CENTER_X * 100, -330, 5, "VS");
  data->status_subtext_id = AddSubtext(data->text, STAGE_CENTER_X * 100, 520, 3.5, "");
  Text_SetColor(data->text, data->status_subtext_id, &dim_color);
}

// The queue, followed by the rest of the room
void InitList() {
  for (int i = 0; i < LIST_LINES; i++) {
    AddSubtext(data->list_text, LIST_LEFT * 100 + 60, -880 + LIST_LINE_GAP * i, 3.5, "");
  }

  data->count_subtext_id = AddSubtext(data->right_text, LIST_RIGHT * 100 - 60, 0, 3.5, "");
}

// Button prompts in the bottom panel
void InitPrompts() {
  for (int i = 0; i < 3; i++) {
    data->prompt_subtext_ids[i] = AddSubtext(data->text, (i - 1) * PROMPT_GAP_X, 1640, 4, "");
  }
}

// Fetch the room from Dolphin, keeping the last one to see what changed
void FetchState() {
  memcpy(data->prev_state, data->state, sizeof(ExiSlippi_GetRoomState_Response));

  data->state_query->command = ExiSlippi_Command_GET_ROOM_STATE;
  ExiSlippi_Transfer(data->state_query, sizeof(ExiSlippi_GetRoomState_Query), ExiSlippi_TransferMode_WRITE);
  ExiSlippi_Transfer(data->state, sizeof(ExiSlippi_GetRoomState_Response), ExiSlippi_TransferMode_READ);
}

void SendAction(u8 action, u8 value0, u8 value1) {
  ExiSlippi_RoomAction_Query *q = data->action_query;
  q->command = ExiSlippi_Command_ROOM_ACTION;
  q->action = action;
  q->value[0] = value0;
  q->value[1] = value1;
  ExiSlippi_Transfer(q, sizeof(ExiSlippi_RoomAction_Query), ExiSlippi_TransferMode_WRITE);
}

void OnStateChange() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  ExiSlippi_GetRoomState_Response *prev = data->prev_state;

  UpdateHeader();
  if (!IsInRoom()) {
    UpdateJoining();
    return;
  }

  // Start the stage cursor on the first stage that isn't struck each turn
  if (state->phase != prev->phase) {
    data->selector_idx = 0;
    while (data->selector_idx < ROOM_STAGE_COUNT - 1 && state->struck[data->selector_idx]) {
      data->selector_idx++;
    }
  }

  // Warn the local player when their time is almost up
  if (IsLocalTurn() && state->turn_seconds == PANIC_SECONDS && prev->turn_seconds != PANIC_SECONDS) {
    SFX_PlayCommon(CommonSound_OFFSCREEN);
  }

  UpdateStage();
  UpdatePrompts();
  UpdateList();
}

// Returns the member's position in the queue, -1 if not queued
int QueuePos(int member) {
  ExiSlippi_GetRoomState_Response *state = data->state;
  for (int i = 0; i < state->queue_count; i++) {
    if (state->queue[i] == member) {
      return i;
    }
  }
  return -1;
}

u8 IsOnSide(int member) {
  return data->state->sides[Room_Side_WINNER] == member || data->state->sides[Room_Side_CHALLENGER] == member;
}

Room_Side LocalSide() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  if (state->sides[Room_Side_WINNER] == state->local_member) {
    return Room_Side_WINNER;
  }
  if (state->sides[Room_Side_CHALLENGER] == state->local_member) {
    return Room_Side_CHALLENGER;
  }
  return Room_Side_NONE;
}

Room_Side TurnSide() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  if (state->phase == Room_Phase_STRIKING) {
    return Room_Side_WINNER;
  }
  if (state->phase == Room_Phase_CHOOSING) {
    return Room_Side_CHALLENGER;
  }
  if (state->phase == Room_Phase_PICKING) {
    return state->has_picked[Room_Side_WINNER] ? Room_Side_CHALLENGER : Room_Side_WINNER;
  }
  return Room_Side_NONE;
}

u8 IsLocalTurn() {
  Room_Side turn = TurnSide();
  return turn != Room_Side_NONE && turn == LocalSide();
}

// Name followed by the member's crown count. 0x817e is the multiplication sign in
// Melee's font
void GetDisplayName(char *out, int member) {
  ExiSlippi_RoomMember *m = &data->state->members[member];
  if (m->crowns == 0) {
    sprintf(out, "%s", m->name);
  } else {
    sprintf(out, "%s  \x81\x7e%d", m->name, m->crowns);
  }
}

// Whether we're hosting the room or have joined it, rather than still joining
u8 IsInRoom() {
  u8 status = data->state->connection_status;
  return status == ExiSlippi_RoomStatus_HOSTING || status == ExiSlippi_RoomStatus_JOINED;
}

// The code arrives once the room is registered, and stays empty if it couldn't be
void UpdateHeader() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  Text_SetText(data->left_text, data->code_subtext_id, "Room %s", state->code[0] ? state->code : "----");

  // Only private rooms have a password
  if (state->password[0]) {
    Text_SetText(data->left_text, data->password_subtext_id, "Password %s", state->password);
  } else {
    Text_SetText(data->left_text, data->password_subtext_id, "");
  }

  if (!IsInRoom()) {
    Text_SetText(data->right_text, data->mode_subtext_id, "");
    Text_SetText(data->right_text, data->stage_mode_subtext_id, "");
    return;
  }

  Text_SetText(data->right_text, data->mode_subtext_id, mode_labels[state->mode]);
  Text_SetText(data->right_text, data->stage_mode_subtext_id, stage_labels[state->stage_mode]);
}

// Shows the join happening, or why it didn't work
void UpdateJoining() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  u8 is_failed = state->connection_status == ExiSlippi_RoomStatus_FAILED;

  for (int i = 0; i < 2; i++) {
    CSBoxSelector_SetVisibility(data->char_selectors[i], false);
    Text_SetText(data->text, data->name_subtext_ids[i], "");
    Text_SetText(data->text, data->code_subtext_ids[i], "");
  }
  CSBoxSelector_SetVisibility(data->stage_selector, false);
  for (int i = 0; i < ROOM_STAGE_COUNT; i++) {
    CSBoxSelector_SetVisibility(data->stage_strike_selectors[i], false);
  }
  Text_SetText(data->text, data->vs_subtext_id, "");
  Text_SetText(data->text, data->streak_subtext_id, "");
  Text_SetText(data->text, data->timer_subtext_id, "");

  u8 error = state->connection_error < sizeof(join_errors) / sizeof(join_errors[0]) ? state->connection_error : 0;
  if (is_failed) {
    Text_SetText(data->text, data->status_subtext_id, "%s", join_errors[error]);
  } else {
    Text_SetText(data->text, data->status_subtext_id, "Joining room %s", state->code);
  }

  for (int i = 0; i < LIST_LINES; i++) {
    Text_SetText(data->list_text, i, "");
  }
  Text_SetText(data->right_text, data->count_subtext_id, "");

  Text_SetText(data->text, data->prompt_subtext_ids[0], "");
  Text_SetText(data->text, data->prompt_subtext_ids[1], is_failed ? "B  Back" : "Hold B  Leave");
  Text_SetText(data->text, data->prompt_subtext_ids[2], "");
}

void UpdateStage() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  Room_Side turn = TurnSide();
  u8 is_drafting = state->phase == Room_Phase_STRIKING || state->phase == Room_Phase_CHOOSING;

  for (int side = 0; side < 2; side++) {
    CSBoxSelector *bs = data->char_selectors[side];
    CSBoxSelector_SetVisibility(bs, !is_drafting);

    int member = state->sides[side];
    if (member < 0) {
      Text_SetText(data->text, data->name_subtext_ids[side], "Waiting");
      Text_SetColor(data->text, data->name_subtext_ids[side], &dim_color);
      Text_SetText(data->text, data->code_subtext_ids[side], "");
    } else {
      char name[40];
      GetDisplayName(name, member);
      Text_SetText(data->text, data->name_subtext_ids[side], "%s", name);
      Text_SetColor(data->text, data->name_subtext_ids[side], &text_color);
      Text_SetText(data->text, data->code_subtext_ids[side], "%s", state->members[member].connect_code);
    }

    // Show a question mark until the side has picked. Random stays hidden until the
    // match starts
    u8 char_id = CKIND_RANDOM;
    u8 char_color = 0;
    if (member >= 0 && state->phase == Room_Phase_PLAYING) {
      char_id = state->play_char[side];
      char_color = state->play_color[side];
    } else if (member >= 0 && state->has_picked[side]) {
      char_id = state->members[member].char_id;
      char_color = state->members[member].char_color;
    }

    u8 has_char = char_id < CKIND_RANDOM;
    if (has_char) {
      CSIcon_SetMaterial(bs->icon, CSIcon_ConvertCharToMat(char_id));
      StockIcon_SetIcon(bs->icon->stock_icon, char_id, char_color);
    } else {
      CSIcon_SetMaterial(bs->icon, CSIcon_Material_Question);
    }
    CSIcon_SetStockIconVisibility(bs->icon, has_char);
    CSIcon_SetSelectState(bs->icon, turn == side && !is_drafting ? CSIcon_Select_State_Blink
                                                                 : CSIcon_Select_State_NotSelected);
  }

  if (state->sides[Room_Side_WINNER] < 0 || state->streak == 0 || is_drafting) {
    Text_SetText(data->text, data->streak_subtext_id, "");
  } else if (state->streak == 1) {
    Text_SetText(data->text, data->streak_subtext_id, "Won the last set");
  } else {
    Text_SetText(data->text, data->streak_subtext_id, "Won %d in a row", state->streak);
  }

  // Show the stage between the players once it's decided
  u8 show_stage = state->phase == Room_Phase_PICKING || state->phase == Room_Phase_PLAYING;
  CSBoxSelector_SetVisibility(data->stage_selector, show_stage);
  CSIcon_SetMaterial(data->stage_selector->icon, stages[state->stage_idx]);
  Text_SetText(data->text, data->vs_subtext_id, show_stage || is_drafting ? "" : "VS");

  u8 is_local_turn = IsLocalTurn();
  for (int i = 0; i < ROOM_STAGE_COUNT; i++) {
    CSBoxSelector *bs = data->stage_strike_selectors[i];
    CSBoxSelector_SetVisibility(bs, is_drafting);
    CSBoxSelector_SetSelectState(
        bs, state->struck[i] ? CSBoxSelector_Select_State_Disabled_X : CSBoxSelector_Select_State_NotSelected);
    CSBoxSelector_SetHover(bs, is_local_turn && is_drafting && data->selector_idx == i);
  }

  // Same timer as ranked, only warning the player whose turn it is
  if (state->turn_seconds == 0xFF) {
    Text_SetText(data->text, data->timer_subtext_id, "");
  } else {
    Text_SetText(data->text, data->timer_subtext_id, "%d:%02d", state->turn_seconds / 60, state->turn_seconds % 60);
    Text_SetColor(data->text, data->timer_subtext_id, &text_color);
    if (is_local_turn && state->turn_seconds <= PANIC_SECONDS) {
      Text_SetColor(data->text, data->timer_subtext_id, &panic_color);
    } else if (is_local_turn && state->turn_seconds <= WARN_SECONDS) {
      Text_SetColor(data->text, data->timer_subtext_id, &warn_color);
    }
  }

  char *turn_name = turn != Room_Side_NONE ? state->members[state->sides[turn]].name : "";
  char status[64];
  if (state->phase == Room_Phase_STRIKING) {
    sprintf(status, "%s strikes a stage", turn_name);
  } else if (state->phase == Room_Phase_CHOOSING) {
    sprintf(status, "%s chooses the stage", turn_name);
  } else if (state->phase == Room_Phase_PICKING) {
    sprintf(status, "%s is picking a character", turn_name);
  } else if (state->phase == Room_Phase_PLAYING && data->handoff == Room_Handoff_FAILED) {
    sprintf(status, "Could not connect, trying again");
  } else if (state->phase == Room_Phase_PLAYING && LocalSide() != Room_Side_NONE) {
    sprintf(status, "Connecting to your opponent");
  } else if (state->phase == Room_Phase_PLAYING) {
    sprintf(status, "Playing on %s", stage_names[state->stage_idx]);
  } else if (state->crowned >= 0) {
    sprintf(status, "%s earned a crown", state->members[state->crowned].name);
  } else if (state->sides[Room_Side_CHALLENGER] >= 0) {
    sprintf(status, "Get ready");
  } else if (state->sides[Room_Side_WINNER] >= 0) {
    sprintf(status, "Waiting for a challenger");
  } else {
    sprintf(status, "Waiting for players");
  }
  Text_SetText(data->text, data->status_subtext_id, "%s", status);
}

void UpdatePrompts() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  char *prompts[3] = {"START  Join the queue", "Hold B  Leave the room", ""};
  if (QueuePos(state->local_member) >= 0) {
    prompts[0] = "Hold Z  Leave the queue";
  } else if (CanChangeColor()) {
    prompts[0] = "X Y  Costume";
    prompts[2] = "Z  Random costume";
  } else if (LocalSide() != Room_Side_NONE) {
    prompts[0] = "";
  }
#ifdef LOCAL_TESTING
  prompts[2] = state->phase == Room_Phase_PLAYING ? "L R  Pick the winner" : "L  Add a player";
#endif
  for (int i = 0; i < 3; i++) {
    Text_SetText(data->text, data->prompt_subtext_ids[i], prompts[i]);
  }
}

void UpdateList() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  int line = 0;
  char name[40];

  Text_SetText(data->list_text, line, "Up next");
  Text_SetColor(data->list_text, line++, &text_color);

  if (state->queue_count == 0) {
    Text_SetText(data->list_text, line, "Nobody in the queue");
    Text_SetColor(data->list_text, line++, &dim_color);
  }
  for (int i = 0; i < state->queue_count && line < LIST_LINES - 2; i++) {
    GetDisplayName(name, state->queue[i]);
    Text_SetText(data->list_text, line, "%d  %s", i + 1, name);
    Text_SetColor(data->list_text, line++, &text_color);
  }

  // Member count next to the heading. 0x815e is the slash in Melee's font
  float y = -880 + LIST_LINE_GAP * line;
  Text_SetText(data->list_text, line, "In the room");
  Text_SetColor(data->list_text, line++, &text_color);
  Text_SetPosition(data->right_text, data->count_subtext_id, LIST_RIGHT * 100 - 60, y);
  if (state->capacity != 0) {
    Text_SetText(data->right_text, data->count_subtext_id, "%d\x81\x5e%d", state->member_count, state->capacity);
  } else {
    Text_SetText(data->right_text, data->count_subtext_id, "%d", state->member_count);
  }

  // Members who aren't playing or queued
  for (int i = 0; i < state->member_count && line < LIST_LINES; i++) {
    if (QueuePos(i) >= 0 || IsOnSide(i)) {
      continue;
    }
    GetDisplayName(name, i);
    Text_SetText(data->list_text, line, "%s", name);
    Text_SetColor(data->list_text, line++, &dim_color);
  }

  while (line < LIST_LINES) {
    Text_SetText(data->list_text, line++, "");
  }
}

// Keep the character picker open during the local player's pick, and close it if the
// turn ends without one, such as when time runs out
void UpdateCharPicker() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  u8 is_picking = state->phase == Room_Phase_PICKING && IsLocalTurn();
  u8 is_open = data->char_picker_dialog->state.is_open;

  if (is_picking && !is_open) {
    ExiSlippi_RoomMember *m = &state->members[state->local_member];
    CharPickerDialog_OpenDialog(data->char_picker_dialog, m->char_id, m->char_color);
  } else if (!is_picking && is_open) {
    CharPickerDialog_CloseDialog(data->char_picker_dialog);
  }
}

// Once the room's match is on, find the opponent the same way direct does, send the room's
// picks, then go to the match once both players are ready
void HandleMatchHandoff() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  Room_Side side = LocalSide();
  if (state->phase != Room_Phase_PLAYING || side == Room_Side_NONE) {
    data->handoff = Room_Handoff_NONE;
    return;
  }

#ifdef LOCAL_TESTING
  // Test players without a real account are played with L and R instead
  ExiSlippi_RoomMember *opp = &state->members[state->sides[!side]];
  if (memcmp(opp->connect_code, "TEST", 4) == 0) {
    return;
  }
#endif

  data->handoff_frames++;

  switch (data->handoff) {
    case Room_Handoff_NONE:
      FindOpponent();
      data->handoff = Room_Handoff_SEARCHING;
      data->handoff_frames = 0;
      break;
    case Room_Handoff_SEARCHING:
      data->match_state = ExiSlippi_LoadMatchState(data->match_state);
      if (data->match_state->mm_state == ExiSlippi_MmState_CONNECTION_SUCCESS) {
        SetMatchSelections();
        data->handoff = Room_Handoff_CONNECTED;
      } else if (data->match_state->mm_state == ExiSlippi_MmState_ERROR_ENCOUNTERED ||
                 data->handoff_frames > CONNECT_TIMEOUT_FRAMES) {
        CleanupConnection();
        data->handoff = Room_Handoff_FAILED;
        data->handoff_frames = 0;
        UpdateStage();
      }
      break;
    case Room_Handoff_CONNECTED:
      data->match_state = ExiSlippi_LoadMatchState(data->match_state);
      if (data->match_state->is_local_player_ready && data->match_state->is_remote_player_ready) {
        data->scene_data->start_match = true;
        data->should_exit = true;
      } else if (data->match_state->mm_state != ExiSlippi_MmState_CONNECTION_SUCCESS) {
        CleanupConnection();
        data->handoff = Room_Handoff_FAILED;
        data->handoff_frames = 0;
        UpdateStage();
      }
      break;
    case Room_Handoff_FAILED:
      if (data->handoff_frames >= CONNECT_RETRY_FRAMES) {
        data->handoff = Room_Handoff_NONE;
        UpdateStage();
      }
      break;
  }
}

void FindOpponent() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  ExiSlippi_RoomMember *opp = &state->members[state->sides[!LocalSide()]];

  ExiSlippi_FindOpponent_Query *q = calloc(sizeof(ExiSlippi_FindOpponent_Query));
  q->command = ExiSlippi_Command_FIND_OPPONENT;
  q->mode = ExiSlippi_OnlineMode_DIRECT;
  memcpy(q->connect_code, opp->connect_code, sizeof(opp->connect_code));
  ExiSlippi_Transfer(q, sizeof(ExiSlippi_FindOpponent_Query), ExiSlippi_TransferMode_WRITE);
}

// Both players send the room's stage, and Pokemon Stadium is never frozen
void SetMatchSelections() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  Room_Side side = LocalSide();

  ExiSlippi_SetSelections_Query *q = calloc(sizeof(ExiSlippi_SetSelections_Query));
  q->command = ExiSlippi_Command_SET_MATCH_SELECTIONS;
  q->team_id = 0;
  q->char_id = state->play_char[side];
  q->char_color_id = state->play_color[side];
  q->char_option = ExiSlippi_SelectionOption_MERGE;
  q->stage_id = CSIcon_ConvertMatToStage(stages[state->stage_idx]);
  q->stage_option = ExiSlippi_SelectionOption_MERGE;
  q->online_mode = ExiSlippi_OnlineMode_DIRECT;
  q->alt_stage_mode = 0;
  ExiSlippi_Transfer(q, sizeof(ExiSlippi_SetSelections_Query), ExiSlippi_TransferMode_WRITE);
}

void CleanupConnection() {
  ExiSlippi_CleanupConnection_Query *q = calloc(sizeof(ExiSlippi_CleanupConnection_Query));
  q->command = ExiSlippi_Command_CLEANUP_CONNECTION;
  ExiSlippi_Transfer(q, sizeof(ExiSlippi_CleanupConnection_Query), ExiSlippi_TransferMode_WRITE);
}

u8 GetVanilaMaxColors(u8 charId) {
  // Returns the maximum number of colors for a character in vanilla Melee. Same as ranked
  switch (charId) {
    case CKIND_FALCON:
    case CKIND_KIRBY:
    case CKIND_YOSHI:
      return 6;
    case CKIND_DK:
    case CKIND_LINK:
    case CKIND_MARIO:
    case CKIND_MARTH:
    case CKIND_PEACH:
    case CKIND_JIGGLYPUFF:
    case CKIND_SAMUS:
    case CKIND_ZELDA:
    case CKIND_SHEIK:
    case CKIND_YOUNGLINK:
    case CKIND_DRMARIO:
    case CKIND_ROY:
    case CKIND_GANONDORF:
      return 5;
    case CKIND_FOX:
    case CKIND_GAW:
    case CKIND_BOWSER:
    case CKIND_LUIGI:
    case CKIND_MEWTWO:
    case CKIND_NESS:
    case CKIND_PIKACHU:
    case CKIND_ICECLIMBERS:
    case CKIND_FALCO:
    case CKIND_PICHU:
      return 4;
    default:
      return 1;  // All other characters have only one color
  }
}

u8 GetNextColor(u8 char_id, u8 color_id, int incr) {
  int max_color = GetVanilaMaxColors(char_id);
  return (color_id + incr + max_color) % max_color;
}

void OnCharSelectionComplete(CharPickerDialog *cpd, u8 is_selection) {
  if (!is_selection || !IsLocalTurn()) {
    return;
  }

  // The picker resolves random before closing, so use where its cursor was last frame
  u8 char_id = data->prev_picker_char >= CKIND_RANDOM ? CKIND_RANDOM : cpd->state.char_selection_idx;
  u8 char_color = char_id < CKIND_RANDOM ? cpd->state.char_color_idx : 0;
  SendAction(ExiSlippi_RoomAction_PICK, char_id, char_color);
  SFX_PlayCommon(CommonSound_ACCEPT);

  // Remember the local player's pick for the next room
  data->scene_data->last_char = char_id;
  data->scene_data->last_color = char_color;
}

// Active players can change color once they have picked a character
u8 CanChangeColor() {
  ExiSlippi_GetRoomState_Response *state = data->state;
  Room_Side side = LocalSide();
  if (side == Room_Side_NONE || state->phase == Room_Phase_PLAYING) {
    return false;
  }

  return state->has_picked[side] && state->members[state->local_member].char_id < CKIND_RANDOM;
}

// X and Y cycle colors, Z picks a random one
void HandleColorInputs(u64 downInputs) {
  ExiSlippi_RoomMember *m = &data->state->members[data->state->local_member];
  u8 char_color = m->char_color;

  if (downInputs & HSD_BUTTON_X) {
    char_color = GetNextColor(m->char_id, char_color, 1);
  } else if (downInputs & HSD_BUTTON_Y) {
    char_color = GetNextColor(m->char_id, char_color, -1);
  } else if (downInputs & HSD_TRIGGER_Z) {
    char_color = HSD_Randi(GetVanilaMaxColors(m->char_id));
  } else {
    return;
  }

  SendAction(ExiSlippi_RoomAction_SET_COLOR, char_color, 0);
  SFX_PlayCommon(CommonSound_NEXT);
  data->scene_data->last_color = char_color;
}

// Leaving the queue (Z) or the room (B) requires holding the button. The prompt fades to
// yellow while it's held
void HandleHoldInputs(u64 heldInputs) {
  int idx = -1;
  if (heldInputs & HSD_TRIGGER_Z && QueuePos(data->state->local_member) >= 0) {
    idx = 0;
  } else if (heldInputs & HSD_BUTTON_B) {
    idx = 1;
  }

  if (idx != data->hold_idx) {
    data->hold_idx = idx;
    data->hold_frames = 0;
  }

  for (int i = 0; i < 3; i++) {
    GXColor col = text_color;
    if (i == idx) {
      float t = (float)data->hold_frames / HOLD_FRAMES;
      col.r += (warn_color.r - text_color.r) * t;
      col.g += (warn_color.g - text_color.g) * t;
      col.b += (warn_color.b - text_color.b) * t;
    }
    Text_SetColor(data->text, data->prompt_subtext_ids[i], &col);
  }

  if (idx < 0 || ++data->hold_frames < HOLD_FRAMES) {
    return;
  }

  data->hold_idx = -1;
  SFX_PlayCommon(CommonSound_BACK);
  if (idx == 0) {
    SendAction(ExiSlippi_RoomAction_LEAVE_QUEUE, 0, 0);
  } else {
    LeaveRoom();
  }
}

void HandleStageInputs(u64 downInputs, u64 scrollInputs) {
  ExiSlippi_GetRoomState_Response *state = data->state;
  int dir = 0;
  if (scrollInputs & (HSD_BUTTON_RIGHT | HSD_BUTTON_DPAD_RIGHT)) {
    dir = 1;
  } else if (scrollInputs & (HSD_BUTTON_LEFT | HSD_BUTTON_DPAD_LEFT)) {
    dir = -1;
  }

  // Skip over struck stages
  if (dir != 0) {
    do {
      data->selector_idx = (data->selector_idx + dir + ROOM_STAGE_COUNT) % ROOM_STAGE_COUNT;
    } while (state->struck[data->selector_idx]);
    SFX_PlayCommon(CommonSound_NEXT);
    UpdateStage();
  } else if (downInputs & HSD_BUTTON_A && state->phase == Room_Phase_STRIKING) {
    SendAction(ExiSlippi_RoomAction_STRIKE, data->selector_idx, 0);
    SFX_PlayCommon(CommonSound_NEXT);
  } else if (downInputs & HSD_BUTTON_A) {
    SendAction(ExiSlippi_RoomAction_CHOOSE, data->selector_idx, 0);
    SFX_PlayCommon(CommonSound_ACCEPT);
  }
}

void LeaveRoom() {
  if (data->handoff != Room_Handoff_NONE) {
    CleanupConnection();
  }

  SendAction(ExiSlippi_RoomAction_LEAVE_ROOM, 0, 0);
  data->should_exit = true;
}

void CObjThink(GOBJ *gobj) {
  COBJ *cobj = gobj->hsd_object;

  if (!CObj_SetCurrent(cobj)) {
    return;
  }

  CObj_SetEraseColor(0, 0, 0, 255);
  CObj_EraseScreen(cobj, 1, 0, 1);
  CObj_RenderGXLinks(gobj, 7);
  CObj_EndCurrent();
}

void InputsThink(GOBJ *gobj) {
  ExiSlippi_GetRoomState_Response *state = data->state;
  u8 port = R13_U8(-0x5108);
  u64 downInputs = Pad_GetDown(port);
  u64 scrollInputs = Pad_GetRapidHeld(port);

  if (data->should_exit) {
    return;
  }

  // Only leaving is possible until the room is joined
  if (!IsInRoom()) {
    if (state->connection_status == ExiSlippi_RoomStatus_FAILED && downInputs & HSD_BUTTON_B) {
      SFX_PlayCommon(CommonSound_BACK);
      LeaveRoom();
    } else {
      HandleHoldInputs(Pad_GetHeld(port));
    }
    return;
  }

  // The picker handles its own inputs. This runs before the picker, so save its cursor
  // before a press resolves random
  if (data->char_picker_dialog->state.is_open) {
    data->prev_picker_char = data->char_picker_dialog->state.char_selection_idx;
    HandleHoldInputs(0);
    return;
  }

  u8 is_drafting = state->phase == Room_Phase_STRIKING || state->phase == Room_Phase_CHOOSING;
  if (is_drafting && IsLocalTurn()) {
    HandleStageInputs(downInputs, scrollInputs);
  }

  if (CanChangeColor()) {
    HandleColorInputs(downInputs);
  }

  if (downInputs & HSD_BUTTON_START && QueuePos(state->local_member) < 0 && !IsOnSide(state->local_member)) {
    SendAction(ExiSlippi_RoomAction_JOIN_QUEUE, 0, 0);
    SFX_PlayCommon(CommonSound_ACCEPT);
  }

  HandleHoldInputs(Pad_GetHeld(port));

#ifdef LOCAL_TESTING
  // L and R pick the winner during a match, otherwise L adds a test player
  if (state->phase == Room_Phase_PLAYING && downInputs & (HSD_TRIGGER_L | HSD_TRIGGER_R)) {
    SendAction(ExiSlippi_RoomAction_FINISH_SET, downInputs & HSD_TRIGGER_L ? Room_Side_WINNER : Room_Side_CHALLENGER, 0);
    SFX_PlayCommon(CommonSound_ACCEPT);
  } else if (downInputs & HSD_TRIGGER_L) {
    SendAction(ExiSlippi_RoomAction_ADD_TEST_PLAYER, 0, 0);
    SFX_PlayCommon(CommonSound_ACCEPT);
  }
#endif
}
