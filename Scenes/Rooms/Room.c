#include "Room.h"

#include "../../Components/CharStageIcon.h"
#include "../../Components/StockIcon.h"
#include "../../Game/Sounds.h"

static HSD_Archive *gui_archive;
static GUI_GameSetup *gui_assets;
static Room_Data *data;

static char *mode_labels[] = {"Singles", "Doubles", "Ironman", "Crews", "Tourney"};
static char *stage_labels[] = {"Random stages", "Stage draft"};

// Starter stages, used for both the draft and random stages
static CSIcon_Material stages[STAGE_COUNT] = {
    CSIcon_Material_Fountain,
    CSIcon_Material_Dreamland,
    CSIcon_Material_Yoshis,
    CSIcon_Material_FinalDestination,
    CSIcon_Material_Battlefield,
    CSIcon_Material_Pokemon,
};
static char *stage_names[STAGE_COUNT] = {
    "Fountain of Dreams",
    "Dreamland",
    "Yoshi's Story",
    "Final Destination",
    "Battlefield",
    "Pokemon Stadium",
};

const GXColor text_color = {255, 255, 255, 255};
const GXColor dim_color = {128, 128, 128, 255};
const GXColor warn_color = {254, 202, 52, 255};
const GXColor panic_color = {255, 50, 50, 255};

void minor_load(Rooms_SceneData *minor_data) {
  data = calloc(sizeof(Room_Data));
  data->scene_data = minor_data;
  LoadOnlineStatus();

  // Generate the room code and password
  data->code = HSD_Randi(10000);
  data->password = HSD_Randi(10000);

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

  // Add the local player as the first member, starting on their last pick
  Room_State *state = &data->state;
  state->sides[Room_Side_WINNER] = -1;
  state->sides[Room_Side_CHALLENGER] = -1;
  state->crowned = -1;
  AddMember(data->online_status->display_name, data->online_status->connect_code);
  state->members[0].char_id = minor_data->last_char;
  state->members[0].char_color = minor_data->last_color;
  data->hold_idx = -1;
  OnTurnChange();
}

void minor_think() {
  Room_State *state = &data->state;
  data->timer_frames++;

  if (data->should_exit) {
    Scene_ExitMinor();
    return;
  }

  u8 is_time_elapsed = UpdateTimer();

  // Start the set once both sides are filled and the delay has passed
  if (state->phase == Room_Phase_WAITING) {
    int delay = state->crowned >= 0 ? CROWN_DELAY_FRAMES : SET_DELAY_FRAMES;
    if (state->sides[Room_Side_CHALLENGER] >= 0 && data->timer_frames >= delay) {
      StartSet();
    }
    return;
  }

  Room_Side side = TurnSide();
  if (side == Room_Side_NONE) {
    return;
  }

  if (is_time_elapsed) {
    TimeOut(side);
    return;
  }

  // Keep the character picker open during the local player's pick
  int member = state->sides[side];
  if (member == 0) {
    if (state->phase == Room_Phase_PICKING && !data->char_picker_dialog->state.is_open) {
      Room_Member *m = &state->members[member];
      CharPickerDialog_OpenDialog(data->char_picker_dialog, m->char_id, m->char_color);
    }
    return;
  }

#ifdef LOCAL_TESTING
  // Take the turn for a test player after a short delay
  if (data->timer_frames < TURN_DELAY_FRAMES) {
    return;
  }

  if (state->phase == Room_Phase_STRIKING || state->phase == Room_Phase_CHOOSING) {
    int idx = HSD_Randi(STAGE_COUNT);
    while (state->struck[idx]) {
      idx = (idx + 1) % STAGE_COUNT;
    }
    if (state->phase == Room_Phase_STRIKING) {
      Strike(idx);
    } else {
      Choose(idx);
    }
  } else if (state->phase == Room_Phase_PICKING) {
    Pick(side, HSD_Randi(CKIND_RANDOM + 1), 0);  // Includes random
  }
#endif
}

void minor_exit(Rooms_SceneData *minor_data) {
}

// Fetch the local player's display name and connect code
void LoadOnlineStatus() {
  data->online_status = calloc(sizeof(ExiSlippi_GetOnlineStatus_Response));

  ExiSlippi_GetOnlineStatus_Query *q = calloc(sizeof(ExiSlippi_GetOnlineStatus_Query));
  q->command = ExiSlippi_Command_GET_ONLINE_STATUS;
  ExiSlippi_Transfer(q, sizeof(ExiSlippi_GetOnlineStatus_Query), ExiSlippi_TransferMode_WRITE);
  ExiSlippi_Transfer(data->online_status, sizeof(ExiSlippi_GetOnlineStatus_Response), ExiSlippi_TransferMode_READ);
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
  Rooms_SceneData *scene_data = data->scene_data;

  char code[16];
  sprintf(code, "Room %04d", data->code);
  AddSubtext(data->left_text, -2800, -1940, 5, code);

  // Only private rooms have a password
  if (scene_data->visibility != 0) {
    char password[16];
    sprintf(password, "Password %04d", data->password);
    int id = AddSubtext(data->left_text, -2800, -1740, 3, password);
    Text_SetColor(data->left_text, id, &dim_color);
  }

  AddSubtext(data->right_text, 2800, -1940, 5, mode_labels[scene_data->mode]);
  int id = AddSubtext(data->right_text, 2800, -1740, 3, stage_labels[scene_data->stage_mode]);
  Text_SetColor(data->right_text, id, &dim_color);

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

  float x = STAGE_CENTER_X - STRIKE_BOX_GAP * (STAGE_COUNT - 1) / 2;
  for (int i = 0; i < STAGE_COUNT; i++) {
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

int AddMember(char *name, char *connect_code) {
  Room_State *state = &data->state;
  if (state->member_count >= ROOM_MAX_MEMBERS) {
    return -1;
  }

  Room_Member *m = &state->members[state->member_count];
  memcpy(m->name, name, sizeof(m->name));
  memcpy(m->connect_code, connect_code, sizeof(m->connect_code));
  m->char_id = CKIND_RANDOM;
  m->char_color = 0;
  return state->member_count++;
}

// Returns the member's position in the queue, -1 if not queued
int QueuePos(int member) {
  Room_State *state = &data->state;
  for (int i = 0; i < state->queue_count; i++) {
    if (state->queue[i] == member) {
      return i;
    }
  }
  return -1;
}

u8 IsOnSide(int member) {
  return data->state.sides[Room_Side_WINNER] == member || data->state.sides[Room_Side_CHALLENGER] == member;
}

void JoinQueue(int member) {
  Room_State *state = &data->state;
  if (QueuePos(member) >= 0 || IsOnSide(member)) {
    return;
  }

  state->queue[state->queue_count++] = member;
  FillSides();
}

void LeaveQueue(int member) {
  Room_State *state = &data->state;

  int pos = QueuePos(member);
  if (pos >= 0) {
    state->queue_count--;
    for (int i = pos; i < state->queue_count; i++) {
      state->queue[i] = state->queue[i + 1];
    }
  }

  // Also give up the member's side if the set hasn't started yet
  if (state->phase == Room_Phase_WAITING && IsOnSide(member)) {
    if (state->sides[Room_Side_WINNER] == member) {
      state->sides[Room_Side_WINNER] = state->sides[Room_Side_CHALLENGER];
      state->has_picked[Room_Side_WINNER] = state->has_picked[Room_Side_CHALLENGER];
      state->streak = 0;
      state->beaten = 0;
    }
    state->sides[Room_Side_CHALLENGER] = -1;
    FillSides();
  }
}

// Moves the next players in the queue onto any empty side between sets
void FillSides() {
  Room_State *state = &data->state;
  if (state->phase != Room_Phase_WAITING) {
    return;
  }

  for (int side = 0; side < 2; side++) {
    if (state->sides[side] >= 0 || state->queue_count == 0) {
      continue;
    }

    state->sides[side] = state->queue[0];
    state->has_picked[side] = false;
    state->queue_count--;
    for (int i = 0; i < state->queue_count; i++) {
      state->queue[i] = state->queue[i + 1];
    }
  }
}

void StartSet() {
  Room_State *state = &data->state;
  memset(state->struck, 0, sizeof(state->struck));
  memset(state->has_picked, 0, sizeof(state->has_picked));
  state->crowned = -1;

  if (data->scene_data->stage_mode != 0) {
    state->phase = Room_Phase_STRIKING;
  } else {
    state->stage = stages[HSD_Randi(STAGE_COUNT)];
    state->phase = Room_Phase_PICKING;
  }

  OnTurnChange();
}

// The winner stays on and the loser goes to the back of the queue
void FinishSet(Room_Side winner) {
  Room_State *state = &data->state;
  int winning_member = state->sides[winner];
  int losing_member = state->sides[!winner];

  if (winner == Room_Side_WINNER) {
    state->streak++;
    state->beaten |= 1 << losing_member;
  } else {
    state->streak = 1;
    state->beaten = 1 << losing_member;
  }
  state->sides[Room_Side_WINNER] = winning_member;
  state->sides[Room_Side_CHALLENGER] = -1;
  state->phase = Room_Phase_WAITING;

  // Keep showing the winner's character from the last set
  state->has_picked[Room_Side_WINNER] = true;

  JoinQueue(losing_member);

  // Beating everyone in the queue earns a crown and sends the winner to the back of it
  if (HasBeatenEveryone()) {
    state->members[winning_member].crowns++;
    state->crowned = winning_member;
    state->streak = 0;
    state->beaten = 0;
    state->sides[Room_Side_WINNER] = -1;
    JoinQueue(winning_member);
  }

  OnTurnChange();
}

// Checks the queue and the next challenger, who may have already stepped up
u8 HasBeatenEveryone() {
  Room_State *state = &data->state;
  int challenger = state->sides[Room_Side_CHALLENGER];
  if (challenger >= 0 && !(state->beaten & 1 << challenger)) {
    return false;
  }

  for (int i = 0; i < state->queue_count; i++) {
    if (!(state->beaten & 1 << state->queue[i])) {
      return false;
    }
  }
  return true;
}

// Name followed by the member's crown count. 0x817e is the multiplication sign in
// Melee's font
void GetDisplayName(char *out, int member) {
  Room_Member *m = &data->state.members[member];
  if (m->crowns == 0) {
    sprintf(out, "%s", m->name);
  } else {
    sprintf(out, "%s  \x81\x7e%d", m->name, m->crowns);
  }
}

Room_Side TurnSide() {
  Room_State *state = &data->state;
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

// In the draft the winner strikes a stage, then the challenger chooses from the rest
void Strike(int idx) {
  data->state.struck[idx] = true;
  data->state.phase = Room_Phase_CHOOSING;
  SFX_PlayCommon(CommonSound_NEXT);
  OnTurnChange();
}

void Choose(int idx) {
  data->state.stage = stages[idx];
  data->state.phase = Room_Phase_PICKING;
  SFX_PlayCommon(CommonSound_ACCEPT);
  OnTurnChange();
}

void Pick(Room_Side side, u8 char_id, u8 char_color) {
  Room_State *state = &data->state;
  int member = state->sides[side];
  state->members[member].char_id = char_id;
  SetColor(member, char_color);
  state->has_picked[side] = true;

  if (state->has_picked[Room_Side_CHALLENGER]) {
    StartMatch();
  }

  SFX_PlayCommon(CommonSound_ACCEPT);
  OnTurnChange();
}

void SetColor(int member, u8 char_color) {
  Room_Member *m = &data->state.members[member];
  m->char_color = char_color;

  // Remember the local player's pick for the next room
  if (member == 0) {
    data->scene_data->last_char = m->char_id;
    data->scene_data->last_color = m->char_color;
  }
}

// Random picks become a random character and color once the match starts
void StartMatch() {
  Room_State *state = &data->state;
  for (int side = 0; side < 2; side++) {
    Room_Member *m = &state->members[state->sides[side]];
    state->play_char[side] = m->char_id;
    state->play_color[side] = m->char_color;

    if (m->char_id >= CKIND_RANDOM) {
      state->play_char[side] = HSD_Randi(CKIND_RANDOM);
      state->play_color[side] = HSD_Randi(GetVanilaMaxColors(state->play_char[side]));
    }
  }

  state->phase = Room_Phase_PLAYING;
}

// Same as ranked's timer. Returns true once time has run out, after the grace period
u8 UpdateTimer() {
  Room_Side turn = TurnSide();
  if (turn == Room_Side_NONE) {
    Text_SetText(data->text, data->timer_subtext_id, "");
    return false;
  }

  int seconds_remaining = TURN_SECONDS - data->timer_frames / 60;
  if (seconds_remaining < 0) {
    seconds_remaining = 0;
  }
  Text_SetText(data->text, data->timer_subtext_id, "%d:%02d", seconds_remaining / 60, seconds_remaining % 60);
  Text_SetColor(data->text, data->timer_subtext_id, &text_color);

  // Only warn the player whose turn it is
  if (data->state.sides[turn] == 0) {
    if (data->timer_frames == 60 * (TURN_SECONDS - PANIC_SECONDS)) {
      SFX_PlayCommon(CommonSound_OFFSCREEN);
    }

    if (seconds_remaining <= PANIC_SECONDS) {
      Text_SetColor(data->text, data->timer_subtext_id, &panic_color);
    } else if (seconds_remaining <= WARN_SECONDS) {
      Text_SetColor(data->text, data->timer_subtext_id, &warn_color);
    }
  }

  return data->timer_frames > 60 * (TURN_SECONDS + GRACE_SECONDS);
}

// Completes the turn with the hovered stage or the member's last character
void TimeOut(Room_Side side) {
  Room_State *state = &data->state;
  if (state->phase == Room_Phase_STRIKING) {
    Strike(data->selector_idx);
  } else if (state->phase == Room_Phase_CHOOSING) {
    Choose(data->selector_idx);
  } else if (state->phase == Room_Phase_PICKING) {
    if (data->char_picker_dialog->state.is_open) {
      CharPickerDialog_CloseDialog(data->char_picker_dialog);
    }

    Room_Member *m = &state->members[state->sides[side]];
    Pick(side, m->char_id, m->char_color);
  }
}

void OnTurnChange() {
  data->timer_frames = 0;

  // Start on the first stage that isn't struck
  data->selector_idx = 0;
  while (data->state.struck[data->selector_idx]) {
    data->selector_idx++;
  }

  UpdateStage();
  UpdateList();
}

// Only restart the timer between sets, so a queue change never resets a turn
void OnQueueChange() {
  if (data->state.phase == Room_Phase_WAITING) {
    data->timer_frames = 0;
  }

  UpdateStage();
  UpdateList();
}

void UpdateStage() {
  Room_State *state = &data->state;
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
  CSIcon_SetMaterial(data->stage_selector->icon, state->stage);
  Text_SetText(data->text, data->vs_subtext_id, show_stage || is_drafting ? "" : "VS");

  u8 is_local_turn = turn != Room_Side_NONE && state->sides[turn] == 0;
  for (int i = 0; i < STAGE_COUNT; i++) {
    CSBoxSelector *bs = data->stage_strike_selectors[i];
    CSBoxSelector_SetVisibility(bs, is_drafting);
    CSBoxSelector_SetSelectState(
        bs, state->struck[i] ? CSBoxSelector_Select_State_Disabled_X : CSBoxSelector_Select_State_NotSelected);
    CSBoxSelector_SetHover(bs, is_local_turn && is_drafting && data->selector_idx == i);
  }

  char *turn_name = turn != Room_Side_NONE ? state->members[state->sides[turn]].name : "";
  char status[64];
  if (state->phase == Room_Phase_STRIKING) {
    sprintf(status, "%s strikes a stage", turn_name);
  } else if (state->phase == Room_Phase_CHOOSING) {
    sprintf(status, "%s chooses the stage", turn_name);
  } else if (state->phase == Room_Phase_PICKING) {
    sprintf(status, "%s is picking a character", turn_name);
  } else if (state->phase == Room_Phase_PLAYING) {
    int idx = 0;
    while (stages[idx] != state->stage) {
      idx++;
    }
    sprintf(status, "Playing on %s", stage_names[idx]);
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

  // Update button prompts
  char *prompts[3] = {"START  Join the queue", "Hold B  Leave the room", ""};
  if (QueuePos(0) >= 0) {
    prompts[0] = "Hold Z  Leave the queue";
  } else if (CanChangeColor()) {
    prompts[0] = "X Y  Costume";
    prompts[2] = "Z  Random costume";
  } else if (IsOnSide(0)) {
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
  Room_State *state = &data->state;
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
  if (data->scene_data->capacity != 0) {
    Text_SetText(data->right_text, data->count_subtext_id, "%d\x81\x5e%d", state->member_count, data->scene_data->capacity);
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
  Room_Side side = TurnSide();
  if (!is_selection || side == Room_Side_NONE) {
    return;
  }

  // The picker resolves random before closing, so use where its cursor was last frame
  u8 char_id = data->prev_picker_char >= CKIND_RANDOM ? CKIND_RANDOM : cpd->state.char_selection_idx;
  Pick(side, char_id, cpd->state.char_color_idx);
}

// Active players can change color once they have picked a character
u8 CanChangeColor() {
  Room_State *state = &data->state;
  if (!IsOnSide(0) || state->phase == Room_Phase_PLAYING) {
    return false;
  }

  Room_Side side = state->sides[Room_Side_WINNER] == 0 ? Room_Side_WINNER : Room_Side_CHALLENGER;
  return state->has_picked[side] && state->members[0].char_id < CKIND_RANDOM;
}

// X and Y cycle colors, Z picks a random one
void HandleColorInputs(u64 downInputs) {
  Room_Member *m = &data->state.members[0];
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

  SetColor(0, char_color);
  SFX_PlayCommon(CommonSound_NEXT);
  UpdateStage();
}

// Leaving the queue (Z) or the room (B) requires holding the button. The prompt fades to
// yellow while it's held
void HandleHoldInputs(u64 heldInputs) {
  int idx = -1;
  if (heldInputs & HSD_TRIGGER_Z && QueuePos(0) >= 0) {
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
    LeaveQueue(0);
    OnQueueChange();
  } else {
    data->should_exit = true;
  }
}

void HandleStageInputs(u64 downInputs, u64 scrollInputs) {
  Room_State *state = &data->state;
  int dir = 0;
  if (scrollInputs & (HSD_BUTTON_RIGHT | HSD_BUTTON_DPAD_RIGHT)) {
    dir = 1;
  } else if (scrollInputs & (HSD_BUTTON_LEFT | HSD_BUTTON_DPAD_LEFT)) {
    dir = -1;
  }

  // Skip over struck stages
  if (dir != 0) {
    do {
      data->selector_idx = (data->selector_idx + dir + STAGE_COUNT) % STAGE_COUNT;
    } while (state->struck[data->selector_idx]);
    SFX_PlayCommon(CommonSound_NEXT);
    UpdateStage();
  } else if (downInputs & HSD_BUTTON_A && state->phase == Room_Phase_STRIKING) {
    Strike(data->selector_idx);
  } else if (downInputs & HSD_BUTTON_A) {
    Choose(data->selector_idx);
  }
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
  Room_State *state = &data->state;
  u8 port = R13_U8(-0x5108);
  u64 downInputs = Pad_GetDown(port);
  u64 scrollInputs = Pad_GetRapidHeld(port);

  // The picker handles its own inputs. This runs before the picker, so save its cursor
  // before a press resolves random
  if (data->char_picker_dialog->state.is_open) {
    data->prev_picker_char = data->char_picker_dialog->state.char_selection_idx;
    HandleHoldInputs(0);
    return;
  }

  Room_Side turn = TurnSide();
  u8 is_drafting = state->phase == Room_Phase_STRIKING || state->phase == Room_Phase_CHOOSING;
  if (is_drafting && turn != Room_Side_NONE && state->sides[turn] == 0) {
    HandleStageInputs(downInputs, scrollInputs);
  }

  if (CanChangeColor()) {
    HandleColorInputs(downInputs);
  }

  if (downInputs & HSD_BUTTON_START && QueuePos(0) < 0 && !IsOnSide(0)) {
    JoinQueue(0);
    SFX_PlayCommon(CommonSound_ACCEPT);
    OnQueueChange();
  }

  HandleHoldInputs(Pad_GetHeld(port));

#ifdef LOCAL_TESTING
  // L and R pick the winner during a match, otherwise L adds a test player
  if (state->phase == Room_Phase_PLAYING && downInputs & (HSD_TRIGGER_L | HSD_TRIGGER_R)) {
    FinishSet(downInputs & HSD_TRIGGER_L ? Room_Side_WINNER : Room_Side_CHALLENGER);
    SFX_PlayCommon(CommonSound_ACCEPT);
  } else if (downInputs & HSD_TRIGGER_L) {
    char name[31];
    char connect_code[10];
    sprintf(name, "Player %d", state->member_count + 1);

    // 0x8194 is the # in Melee's font
    sprintf(connect_code, "TEST\x81\x94%d", state->member_count + 1);
    int member = AddMember(name, connect_code);
    if (member >= 0) {
      JoinQueue(member);
      SFX_PlayCommon(CommonSound_ACCEPT);
      OnQueueChange();
    }
  }
#endif
}
