#include "Rooms.h"

#include "../../Game/Sounds.h"

static HSD_Archive *trophy_archive;
static HSD_Archive *gui_archive;
static Rooms_Data *data;

// Public rooms, filled in once the list comes from the server
static Rooms_Room rooms[LIST_ROWS];
static int room_count = 0;

static char *main_labels[PILL_COUNT] = {"Create", "Join", "Mode", "Region", "Random"};
static char *mode_labels[PILL_COUNT] = {"Singles", "Doubles", "Ironman", "Crews", "Tourney"};
static char *region_labels[PILL_COUNT] = {"NAE", "NAW", "EU", "LA", "AS"};
static char *create_labels[CREATE_PILL_COUNT] = {"Create", "Cancel"};
static char *join_labels[JOIN_PILL_COUNT] = {"Join", "Cancel"};

static char *visibility_labels[] = {"Public", "Private"};
static char *size_labels[] = {"2", "4", "8", "16", "32", "No limit"};
static char *stage_labels[] = {"Random", "Draft"};

// The create form's settings, top to bottom. The modes and sizes are drawn narrower
// to fit their bars. Sizes and stage picks are the Singles set for now
static Rooms_SettingInfo settings[CREATE_SETTINGS] = {
    {"VISIBILITY", visibility_labels, 2, ROW_NAME_SCALE_X},
    {"MODE", mode_labels, PILL_COUNT, CREATE_NARROW_SCALE_X},
    {"LOBBY SIZE", size_labels, 6, CREATE_NARROW_SCALE_X},
    {"STAGES", stage_labels, 2, ROW_NAME_SCALE_X},
};

// Public, Singles, 8 players, random stages
static int default_choices[CREATE_SETTINGS] = {0, 0, 2, 0};

const GXColor pill_off = {110, 230, 80, 255};
const GXColor pill_on = {255, 220, 60, 255};
const GXColor pill_hover = {255, 255, 255, 255};
const GXColor pill_hover_on = {255, 255, 170, 255};

// The trophy list's text colors: black on the selected row, white on the rest
const GXColor row_color = {255, 255, 255, 255};
const GXColor row_selected_color = {0, 0, 0, 255};

// The form labels match the panel's green, and the choices not picked are dimmed
const GXColor label_color = {150, 215, 140, 255};
const GXColor unchosen_color = {70, 70, 80, 255};
const GXColor unchosen_selected_row_color = {150, 150, 160, 255};

void minor_load(void *minor_data) {
  data = calloc(sizeof(Rooms_Data));

  // Set up input handler. Initialize at top to make sure it runs before anything else
  GOBJ *input_handler_gobj = GObj_Create(4, 0, 128);
  GObj_AddProc(input_handler_gobj, InputsThink, 0);

  // The trophy gallery's list screen, straight from the disc
  trophy_archive = Archive_LoadFile("TyMnView.usd");

  // Rooms' own header and side label
  gui_archive = Archive_LoadFile("Rooms_gui.dat");

  GOBJ *cam_gobj = GObj_Create(2, 3, 128);
  COBJ *cam_cobj = COBJ_LoadDesc(Archive_GetPublicAddress(trophy_archive, "ScMenFigure_cam_int1_camera"));
  GObj_AddObject(cam_gobj, 1, cam_cobj);
  GOBJ_InitCamera(cam_gobj, CObjThink, 0);

  // Indicates which gx_links to render
  cam_gobj->cobj_links = (1 << 0) + (1 << 1) + (1 << 2) + (1 << 3) + (1 << 4);

  // The trophy list draws its row text in the scene through a second copy of the
  // camera, which renders only the text, after everything else
  GOBJ *text_cam_gobj = GObj_Create(2, 3, 128);
  COBJ *text_cam_cobj = COBJ_LoadDesc(Archive_GetPublicAddress(trophy_archive, "ScMenFigure_cam_int1_camera"));
  GObj_AddObject(text_cam_gobj, 1, text_cam_cobj);
  GOBJ_InitCamera(text_cam_gobj, TextCObjThink, 1);
  text_cam_gobj->cobj_links = 1 << ROW_TEXT_GXLINK;
  data->row_canvas = Text_CreateCanvas(0, (int)text_cam_gobj, 11, 11, 0, ROW_TEXT_GXLINK, 0, 0);

  // create lights
  GOBJ *light_gobj = GObj_Create(3, 4, 128);
  LOBJ *lobj = LObj_LoadAll(Archive_GetPublicAddress(trophy_archive, "ScMenFigure_scene_lights"));
  GObj_AddObject(light_gobj, 2, lobj);
  GObj_AddGXLink(light_gobj, GXLink_LObj, 0, 128);

  // create background
  LoadModel("ToyFigureBack_Top_joint", "ToyFigureBack_Top_animjoint", "ToyFigureBack_Top_matanim_joint");

  // A second panel supplies pills four and five. Everything else on it is hidden.
  // Pills overlap their right neighbour, so the rightmost ones have to be drawn first
  JOBJ *extra = LoadPanel();
  for (int i = 1; i <= PANEL_SERIES_JOINT; i++) {
    if (i >= PANEL_PILL_RIGHT && i <= PANEL_PILL_MIDDLE + 1) {
      continue;
    }
    HideJoint(GetJoint(extra, i));
  }

  // Frame and list panel, without the trophy counter or series logo
  JOBJ *panel = LoadPanel();
  HideJoint(GetJoint(panel, PANEL_COUNTER_JOINT));
  HideJoint(GetJoint(panel, PANEL_SERIES_JOINT));

  // "Online Play | Rooms" and "PUBLIC" in place of "Trophies | Gallery" and "TROPHY LIST".
  // Only this screen's copy changes, so the trophy gallery itself is untouched
  DOBJ *header = GetJoint(panel, PANEL_HEADER_JOINT)->dobj;
  SetLabel(header->next, "RoomsHeaderLeft_image");
  SetLabel(header->next->next, "RoomsHeaderRight_image");
  data->side_label = GetJoint(panel, PANEL_SIDE_LABEL_JOINT);
  SetLabel(data->side_label->dobj, "RoomsSideLabel_image");

  // Five pills where the trophy screen has three, stretched to where its counter
  // ended and with the same margin at both ends of the bar
  float gap = GetJoint(panel, PANEL_PILL_MIDDLE)->trans.X - GetJoint(panel, PANEL_PILL_LEFT)->trans.X;
  float first = GetJoint(panel, PANEL_PILL_LEFT)->trans.X - PILL_SHIFT * gap;
  float step = PILL_SCALE * gap;
  data->pills[0] = InitPill(panel, PANEL_PILL_LEFT, first, PILL_SCALE);
  data->pills[1] = InitPill(panel, PANEL_PILL_MIDDLE, first + step, PILL_SCALE);
  data->pills[2] = InitPill(panel, PANEL_PILL_RIGHT, first + step * 2, PILL_SCALE);
  data->pills[3] = InitPill(extra, PANEL_PILL_MIDDLE, first + step * 3, PILL_SCALE);
  data->pills[4] = InitPill(extra, PANEL_PILL_RIGHT, first + step * 4, PILL_SCALE);

  InitRows(panel);

  // Pill labels, drawn in front of the pills and centered on each one
  data->pill_text = Text_CreateText(0, 0);
  data->pill_text->kerning = 1;
  data->pill_text->align = 1;
  data->pill_text->use_aspect = 1;
  data->pill_text->scale = (Vec2){0.01, 0.01};
  data->pill_text->trans.Z = 5;
  for (int i = 0; i < PILL_COUNT; i++) {
    data->pill_subtexts[i] = Text_AddSubtext(data->pill_text, -1922 + i * 842, 1285, "");
    Text_SetScale(data->pill_text, data->pill_subtexts[i], 3.8, 3.8);
  }

  data->focus = Rooms_Focus_LIST;
  UpdateList();
  UpdatePills();
}

void minor_think() {
  if (data->should_exit) {
    Scene_ExitMinor();
  }
}

void minor_exit(void *minor_data) {
}

GOBJ *LoadModel(char *joint, char *animjoint, char *matanimjoint) {
  JOBJSet *set = calloc(sizeof(JOBJSet));
  set->jobj = Archive_GetPublicAddress(trophy_archive, joint);

  set->animjoint = calloc(sizeof(void *) * 2);
  if (animjoint) {
    set->animjoint[0] = Archive_GetPublicAddress(trophy_archive, animjoint);
  }

  set->matanimjoint = calloc(sizeof(void *) * 2);
  if (matanimjoint) {
    set->matanimjoint[0] = Archive_GetPublicAddress(trophy_archive, matanimjoint);
  }

  set->shapeaninjoint = calloc(sizeof(void *) * 2);

  // gx_link 0 so every model is drawn before the text
  return JOBJ_LoadSet(0, set, 0, 0, 3, 0, 1, GObj_Anim);
}

// Holds a model on one frame of its animation
JOBJ *FreezeModel(GOBJ *gobj, float frame) {
  GObj_RemoveProc(gobj);

  JOBJ *jobj = gobj->hsd_object;
  JOBJ_ReqAnimAll(jobj, frame);
  JOBJ_AnimAll(jobj);
  return jobj;
}

// The trophy panel, held on the frame where the list is fully in view
JOBJ *LoadPanel() {
  GOBJ *gobj = LoadModel("ToyFigurePanel_Top_joint", "ToyFigurePanel_Top_animjoint", "ToyFigurePanel_Top_matanim_joint");
  return FreezeModel(gobj, PANEL_LIST_FRAME);
}

// Joints are numbered depth first, the same order HSDRawViewer lists them in
JOBJ *GetJoint(JOBJ *root, int index) {
  JOBJ *jobj = 0;
  JOBJ_GetChild(root, &jobj, index, -1);
  return jobj;
}

void SetLabel(DOBJ *dobj, char *image) {
  dobj->mobj->tobj->imagedesc = Archive_GetPublicAddress(gui_archive, image);
}

void HideJoint(JOBJ *jobj) {
  if (!jobj) {
    return;
  }

  for (DOBJ *d = jobj->dobj; d; d = d->next) {
    d->flags |= DOBJ_HIDDEN;
  }
}

void ShowJoint(JOBJ *jobj) {
  if (!jobj) {
    return;
  }

  for (DOBJ *d = jobj->dobj; d; d = d->next) {
    d->flags &= ~DOBJ_HIDDEN;
  }
}

JOBJ *InitPill(JOBJ *panel, int joint, float x, float scale) {
  JOBJ *jobj = GetJoint(panel, joint);
  jobj->trans.X = x;
  jobj->scale.X = scale;
  JOBJ_SetMtxDirtySub(jobj);

  // The word is drawn as text instead, so it can be anything
  DOBJ *shape = jobj->child->dobj;
  shape->next->flags |= DOBJ_HIDDEN;

  // The child holds the pill's look, which the trophy screen switches by frame
  return jobj->child;
}

// One trophy list bar per row, spaced the way the panel's two row markers are
void InitRows(JOBJ *panel) {
  Vec3 row_2;
  JOBJ_GetWorldPosition(GetJoint(panel, PANEL_ROW_1_JOINT), 0, &data->row_pos);
  JOBJ_GetWorldPosition(GetJoint(panel, PANEL_ROW_2_JOINT), 0, &row_2);
  data->row_step.X = row_2.X - data->row_pos.X;
  data->row_step.Y = row_2.Y - data->row_pos.Y;
  data->row_step.Z = row_2.Z - data->row_pos.Z;

  for (int i = 0; i < LIST_ROWS; i++) {
    GOBJ *gobj = LoadModel("ToyFigureListBase_Top_joint", 0, "ToyFigureListBase_Top_matanim_joint");
    data->rows[i] = FreezeModel(gobj, 0);
    HideJoint(GetJoint(data->rows[i], ROW_SERIES_JOINT));
    SetRowPos(data->rows[i], i);

    // Three texts per row like the trophy list: code, host and region, then mode, then count
    data->name_text[i] = CreateRowText(i, ROW_CODE_X, ROW_NAME_SCALE_X, ROW_NAME_WIDTH);
    Text_AddSubtext(data->name_text[i], (ROW_NAME_X - ROW_CODE_X) / ROW_NAME_SCALE_X, 0, "");
    Text_AddSubtext(data->name_text[i], (ROW_REGION_X - ROW_CODE_X) / ROW_NAME_SCALE_X, 0, "");
    data->mode_text[i] = CreateRowText(i, ROW_MODE_X, ROW_NAME_SCALE_X, ROW_NAME_WIDTH);
    data->size_text[i] = CreateRowText(i, ROW_COUNT_X, ROW_COUNT_SCALE_X, ROW_COUNT_WIDTH);
    data->size_text[i]->align = 2;
  }

  data->empty_text = CreateRowText(0, ROW_CODE_X, ROW_NAME_SCALE_X, ROW_NAME_WIDTH);

  // Each create setting's choices sit on the bar below its label
  for (int i = 0; i < CREATE_SETTINGS; i++) {
    data->choice_text[i] = CreateSlotText(i * 2 + 1, settings[i].count, settings[i].scale_x);
  }

  // The join screen's code and password bars, and the keypad below them
  data->code_text = CreateSlotText(JOIN_CODE_ROW, CODE_DIGITS, ROW_COUNT_SCALE_X);
  data->password_text = CreateSlotText(JOIN_PASSWORD_ROW, PASSWORD_DIGITS, ROW_COUNT_SCALE_X);
  for (int i = 0; i < KEYPAD_ROWS; i++) {
    data->keypad_text[i] = CreateKeypadText(KEYPAD_FIRST_ROW + i);
  }

  // A bar for the highlighted keypad key, under the cursor so it lights up like a row
  GOBJ *key_bar = LoadModel("ToyFigureListBase_Top_joint", 0, "ToyFigureListBase_Top_matanim_joint");
  data->key_bar = FreezeModel(key_bar, 0);
  HideJoint(GetJoint(data->key_bar, ROW_SERIES_JOINT));

  GOBJ *cursor = LoadModel("ToyFigureListCursor_Top_joint", 0, 0);
  data->cursor = cursor->hsd_object;

  GOBJ *slot_cursor = LoadModel("ToyFigureListCursor_Top_joint", 0, 0);
  data->slot_cursor = slot_cursor->hsd_object;

  // The red line under the last row
  GOBJ *list_end = LoadModel("ToyFigureListBaseend_Top_joint", 0, 0);
  data->list_end = list_end->hsd_object;
}

Text *CreateRowText(int row, float x, float scale_x, float width) {
  float row_x = data->row_pos.X + data->row_step.X * row;
  float row_y = data->row_pos.Y + data->row_step.Y * row;
  float row_z = data->row_pos.Z + data->row_step.Z * row;

  Text *text = Text_CreateText(0, data->row_canvas);

  // Text runs down the screen while the scene runs up, so the row's height is flipped
  text->trans = (Vec3){row_x + x, -row_y - ROW_TEXT_Y, row_z};
  text->aspect = (Vec2){width, ROW_TEXT_HEIGHT};
  text->kerning = 1;
  text->scale = (Vec2){scale_x, ROW_TEXT_SCALE_Y};
  Text_AddSubtext(text, 0, 0, "");
  return text;
}

// A line for each slot on a row's bar, for the create form's choices and the join
// screen's digits
Text *CreateSlotText(int row, int count, float scale_x) {
  float slot = (FORM_RIGHT_X - FORM_LEFT_X) / count;

  // Centered in equal slots across the bar. Line positions are in the text's own units
  Text *text = CreateRowText(row, FORM_LEFT_X, scale_x, ROW_NAME_WIDTH);
  text->align = 1;
  Text_SetPosition(text, 0, slot / 2 / scale_x, 0);
  for (int i = 1; i < count; i++) {
    Text_AddSubtext(text, (slot * i + slot / 2) / scale_x, 0, "");
  }
  return text;
}

// One row of keys, centered on the list area like a phone's keypad
Text *CreateKeypadText(int row) {
  Text *text = CreateRowText(row, BAR_CENTER_X - KEYPAD_GAP_X, KEYPAD_SCALE_X, ROW_NAME_WIDTH);
  text->align = 1;
  for (int i = 1; i < KEYPAD_COLUMNS; i++) {
    Text_AddSubtext(text, KEYPAD_GAP_X * i / KEYPAD_SCALE_X, 0, "");
  }
  return text;
}

void SetRowPos(JOBJ *jobj, float row) {
  jobj->trans.X = data->row_pos.X + data->row_step.X * row;
  jobj->trans.Y = data->row_pos.Y + data->row_step.Y * row;
  jobj->trans.Z = data->row_pos.Z + data->row_step.Z * row;
  JOBJ_SetMtxDirtySub(jobj);
}

u8 IsPillOn(int idx) {
  switch (data->pill_row) {
    case Rooms_PillRow_MAIN:
      if (idx == Rooms_MainPill_MODE) {
        return data->mode_filter != 0;
      }
      if (idx == Rooms_MainPill_REGION) {
        return data->region_filter != 0;
      }
      return false;
    case Rooms_PillRow_MODE:
      return (data->mode_filter >> idx) & 1;
    case Rooms_PillRow_REGION:
      return (data->region_filter >> idx) & 1;
    default:
      return false;
  }
}

int PillCount() {
  if (data->pill_row == Rooms_PillRow_CREATE) {
    return CREATE_PILL_COUNT;
  }
  if (data->pill_row == Rooms_PillRow_JOIN) {
    return JOIN_PILL_COUNT;
  }
  return PILL_COUNT;
}

void UpdatePills() {
  char **labels = main_labels;
  if (data->pill_row == Rooms_PillRow_MODE) {
    labels = mode_labels;
  } else if (data->pill_row == Rooms_PillRow_REGION) {
    labels = region_labels;
  } else if (data->pill_row == Rooms_PillRow_CREATE) {
    labels = create_labels;
  } else if (data->pill_row == Rooms_PillRow_JOIN) {
    labels = join_labels;
  }

  for (int i = 0; i < PILL_COUNT; i++) {
    // The create and join screens only use the first pills
    if (i >= PillCount()) {
      JOBJ_SetFlagsAll(data->pills[i], JOBJ_HIDDEN);
      Text_SetText(data->pill_text, data->pill_subtexts[i], "");
      continue;
    }
    JOBJ_ClearFlagsAll(data->pills[i], JOBJ_HIDDEN);

    u8 is_on = IsPillOn(i);
    u8 is_hover = data->focus == Rooms_Focus_PILLS && data->pill_idx == i;

    Text_SetText(data->pill_text, data->pill_subtexts[i], labels[i]);
    const GXColor *col = is_on ? &pill_on : &pill_off;
    if (is_hover) {
      col = is_on ? &pill_hover_on : &pill_hover;
    }
    Text_SetColor(data->pill_text, data->pill_subtexts[i], col);

    // Frame 1 is the trophy screen's selected tab, frame 0 the others
    JOBJ_ReqAnim(data->pills[i], is_hover ? 1 : 0);
    JOBJ_Anim(data->pills[i]);
  }
}

void ClearRow(int row) {
  JOBJ_SetFlagsAll(data->rows[row], JOBJ_HIDDEN);
  Text_SetText(data->name_text[row], 0, "");
  Text_SetText(data->name_text[row], 1, "");
  Text_SetText(data->name_text[row], 2, "");
  Text_SetText(data->mode_text[row], 0, "");
  Text_SetText(data->size_text[row], 0, "");
}

// Empties the list area so the list, create form or join screen can draw into it
void ClearListArea() {
  for (int i = 0; i < LIST_ROWS; i++) {
    ClearRow(i);
  }
  for (int i = 0; i < CREATE_SETTINGS; i++) {
    for (int j = 0; j < settings[i].count; j++) {
      Text_SetText(data->choice_text[i], j, "");
    }
  }
  for (int i = 0; i < CODE_DIGITS; i++) {
    Text_SetText(data->code_text, i, "");
  }
  for (int i = 0; i < PASSWORD_DIGITS; i++) {
    Text_SetText(data->password_text, i, "");
  }
  for (int i = 0; i < KEYPAD_ROWS; i++) {
    for (int j = 0; j < KEYPAD_COLUMNS; j++) {
      Text_SetText(data->keypad_text[i], j, "");
    }
  }
  Text_SetText(data->empty_text, 0, "");
  JOBJ_SetFlagsAll(data->list_end, JOBJ_HIDDEN);
  JOBJ_SetFlagsAll(data->cursor, JOBJ_HIDDEN);
  JOBJ_SetFlagsAll(data->key_bar, JOBJ_HIDDEN);
  JOBJ_SetFlagsAll(data->slot_cursor, JOBJ_HIDDEN);
}

// Shows a model on a row, moved right by x and narrowed by scale_x
void PlaceModel(JOBJ *jobj, float row, float x, float scale_x) {
  JOBJ_ClearFlagsAll(jobj, JOBJ_HIDDEN);
  jobj->scale.X = scale_x;
  SetRowPos(jobj, row);
  jobj->trans.X += x;
}

void UpdateList() {
  ClearListArea();
  if (data->screen == Rooms_Screen_CREATE) {
    UpdateCreate();
    return;
  }
  if (data->screen == Rooms_Screen_JOIN) {
    UpdateJoin();
    return;
  }

  ShowJoint(data->side_label);

  data->visible_count = 0;
  for (int i = 0; i < room_count && data->visible_count < LIST_ROWS; i++) {
    Rooms_Room *room = &rooms[i];
    if (data->mode_filter && !((data->mode_filter >> room->mode) & 1)) {
      continue;
    }
    if (data->region_filter && !((data->region_filter >> room->region) & 1)) {
      continue;
    }
    data->visible[data->visible_count++] = i;
  }

  if (data->row_idx >= data->visible_count) {
    data->row_idx = data->visible_count > 0 ? data->visible_count - 1 : 0;
  }
  if (data->visible_count == 0) {
    data->focus = Rooms_Focus_PILLS;
  }

  for (int i = 0; i < LIST_ROWS; i++) {
    if (i >= data->visible_count) {
      continue;
    }

    JOBJ_ClearFlagsAll(data->rows[i], JOBJ_HIDDEN);

    Rooms_Room *room = &rooms[data->visible[i]];
    Text_SetText(data->name_text[i], 0, room->code);
    Text_SetText(data->name_text[i], 1, room->host);
    Text_SetText(data->name_text[i], 2, region_labels[room->region]);
    Text_SetText(data->mode_text[i], 0, mode_labels[room->mode]);
    // 0x815e is the slash in Melee's font
    Text_SetText(data->size_text[i], 0, "%d\x81\x5e%d", room->players, room->capacity);
    data->size_text[i]->scale.X = room->capacity >= 10 ? ROW_COUNT_LONG_SCALE_X : ROW_COUNT_SCALE_X;

    const GXColor *col = &row_color;
    if (data->focus == Rooms_Focus_LIST && data->row_idx == i) {
      col = &row_selected_color;
    }
    Text_SetColor(data->name_text[i], 0, col);
    Text_SetColor(data->name_text[i], 1, col);
    Text_SetColor(data->name_text[i], 2, col);
    Text_SetColor(data->mode_text[i], 0, col);
    Text_SetColor(data->size_text[i], 0, col);
  }

  if (data->visible_count == 0) {
    char *msg = data->mode_filter || data->region_filter ? "No rooms match these filters" : "No public rooms right now";
    Text_SetText(data->empty_text, 0, msg);
  } else {
    JOBJ_ClearFlagsAll(data->list_end, JOBJ_HIDDEN);
    SetRowPos(data->list_end, data->visible_count - 1);
  }

  if (data->focus == Rooms_Focus_LIST) {
    PlaceModel(data->cursor, data->row_idx, 0, 1);
  }
}

// The create form, in the list area: each setting's label, then its choices on a bar
void UpdateCreate() {
  HideJoint(data->side_label);

  for (int i = 0; i < CREATE_SETTINGS; i++) {
    int label_row = i * 2;
    u8 is_current = data->focus == Rooms_Focus_LIST && data->setting_idx == i;

    Text_SetText(data->name_text[label_row], 0, settings[i].title);
    Text_SetColor(data->name_text[label_row], 0, &label_color);
    JOBJ_ClearFlagsAll(data->rows[label_row + 1], JOBJ_HIDDEN);

    for (int j = 0; j < settings[i].count; j++) {
      const GXColor *col = is_current ? &unchosen_selected_row_color : &unchosen_color;
      if (data->choices[i] == j) {
        col = is_current ? &row_selected_color : &row_color;
      }
      Text_SetText(data->choice_text[i], j, settings[i].choices[j]);
      Text_SetColor(data->choice_text[i], j, col);
    }
  }

  if (data->focus == Rooms_Focus_LIST) {
    PlaceModel(data->cursor, data->setting_idx * 2 + 1, 0, 1);
  }
}

// Shows count typed digits from entry[first] on a row's bar, with a dash for each one
// still to type. The next digit's slot is highlighted, so its dash is dark like a
// selected row's text
void UpdateEntry(Text *text, int row, int first, int count) {
  for (int i = 0; i < count; i++) {
    int digit = first + i;
    if (digit < data->entry_len) {
      Text_SetText(text, i, "%d", data->entry[digit]);
    } else {
      Text_SetText(text, i, "-");
    }
    Text_SetColor(text, i, digit == data->entry_len ? &row_selected_color : &row_color);
  }

  if (data->entry_len >= first && data->entry_len < first + count) {
    float slot = (FORM_RIGHT_X - FORM_LEFT_X) / count;
    float x = FORM_LEFT_X + slot * (data->entry_len - first + 0.5) - BAR_CENTER_X * SLOT_SCALE;
    PlaceModel(data->slot_cursor, row, x, SLOT_SCALE);
  }
}

// The join screen: code and password bars, then the keypad
void UpdateJoin() {
  HideJoint(data->side_label);

  // Each label sits on the row above its bar
  Text_SetText(data->name_text[JOIN_CODE_ROW - 1], 0, "ROOM");
  Text_SetColor(data->name_text[JOIN_CODE_ROW - 1], 0, &label_color);
  Text_SetText(data->name_text[JOIN_PASSWORD_ROW - 1], 0, "PASSWORD");
  Text_SetColor(data->name_text[JOIN_PASSWORD_ROW - 1], 0, &label_color);
  JOBJ_ClearFlagsAll(data->rows[JOIN_CODE_ROW], JOBJ_HIDDEN);
  JOBJ_ClearFlagsAll(data->rows[JOIN_PASSWORD_ROW], JOBJ_HIDDEN);
  UpdateEntry(data->code_text, JOIN_CODE_ROW, 0, CODE_DIGITS);
  UpdateEntry(data->password_text, JOIN_PASSWORD_ROW, CODE_DIGITS, PASSWORD_DIGITS);

  // 1 to 9 in rows of three, then 0 under the 8
  for (int i = 0; i < KEYPAD_ROWS - 1; i++) {
    for (int j = 0; j < KEYPAD_COLUMNS; j++) {
      Text_SetText(data->keypad_text[i], j, "%d", i * KEYPAD_COLUMNS + j + 1);
    }
  }
  Text_SetText(data->keypad_text[KEYPAD_ROWS - 1], 1, "0");

  for (int i = 0; i < KEYPAD_ROWS; i++) {
    for (int j = 0; j < KEYPAD_COLUMNS; j++) {
      u8 is_current = data->focus == Rooms_Focus_LIST && data->key_row == i && data->key_col == j;
      Text_SetColor(data->keypad_text[i], j, is_current ? &row_selected_color : &row_color);
    }
  }

  if (data->focus == Rooms_Focus_LIST) {
    float key_x = BAR_CENTER_X + (data->key_col - 1) * KEYPAD_GAP_X;
    float x = key_x - BAR_CENTER_X * KEY_SCALE;
    PlaceModel(data->key_bar, KEYPAD_FIRST_ROW + data->key_row, x, KEY_SCALE);
    PlaceModel(data->cursor, KEYPAD_FIRST_ROW + data->key_row, x, KEY_SCALE);
  }
}

void OpenCreate() {
  data->screen = Rooms_Screen_CREATE;
  data->pill_row = Rooms_PillRow_CREATE;
  data->focus = Rooms_Focus_LIST;
  data->setting_idx = 0;
  for (int i = 0; i < CREATE_SETTINGS; i++) {
    data->choices[i] = default_choices[i];
  }
}

void OpenJoin() {
  data->screen = Rooms_Screen_JOIN;
  data->pill_row = Rooms_PillRow_JOIN;
  data->focus = Rooms_Focus_LIST;
  data->entry_len = 0;
  data->key_row = 0;
  data->key_col = 0;
}

// Back to the room list, with the cursor on the pill that opened the form
void CloseForm() {
  data->pill_idx = data->screen == Rooms_Screen_JOIN ? Rooms_MainPill_JOIN : Rooms_MainPill_CREATE;
  data->screen = Rooms_Screen_LIST;
  data->pill_row = Rooms_PillRow_MAIN;
  data->focus = Rooms_Focus_PILLS;
}

void PlayAlert() {
  SFX_PlayRaw(0xbc, 127, 64, 0, 0);  // Same warning sound as logging out
}

void HandlePillPress() {
  if (data->pill_row == Rooms_PillRow_CREATE) {
    if (data->pill_idx == Rooms_CreatePill_CANCEL) {
      SFX_PlayCommon(CommonSound_BACK);
      CloseForm();
    } else {
      // Creating a room is not built yet
      PlayAlert();
    }
    return;
  }

  if (data->pill_row == Rooms_PillRow_JOIN) {
    if (data->pill_idx == Rooms_JoinPill_CANCEL) {
      SFX_PlayCommon(CommonSound_BACK);
      CloseForm();
    } else if (data->entry_len != CODE_DIGITS && data->entry_len != ENTRY_DIGITS) {
      // A full code, and either no password or a full one
      SFX_PlayCommon(CommonSound_ERROR);
    } else {
      // Joining a room is not built yet
      PlayAlert();
    }
    return;
  }

  if (data->pill_row != Rooms_PillRow_MAIN) {
    if (data->pill_row == Rooms_PillRow_MODE) {
      data->mode_filter ^= 1 << data->pill_idx;
    } else {
      data->region_filter ^= 1 << data->pill_idx;
    }
    SFX_PlayCommon(CommonSound_ACCEPT);
    return;
  }

  switch (data->pill_idx) {
    case Rooms_MainPill_MODE:
      data->pill_row = Rooms_PillRow_MODE;
      data->pill_idx = 0;
      SFX_PlayCommon(CommonSound_ACCEPT);
      break;
    case Rooms_MainPill_REGION:
      data->pill_row = Rooms_PillRow_REGION;
      data->pill_idx = 0;
      SFX_PlayCommon(CommonSound_ACCEPT);
      break;
    case Rooms_MainPill_RANDOM:
      if (data->visible_count == 0) {
        PlayAlert();
      } else {
        SFX_PlayCommon(CommonSound_ACCEPT);
      }
      break;
    case Rooms_MainPill_CREATE:
      OpenCreate();
      SFX_PlayCommon(CommonSound_ACCEPT);
      break;
    case Rooms_MainPill_JOIN:
      OpenJoin();
      SFX_PlayCommon(CommonSound_ACCEPT);
      break;
  }
}

// Moves around the keypad, types the key with A and deletes with B, or closes the
// screen when there is nothing left to delete. Down from the 0 moves to the pills
u8 HandleKeypadInputs(u8 up, u8 down, u8 left, u8 right, u64 downInputs) {
  u8 on_zero = data->key_row == KEYPAD_ROWS - 1;

  if (up && data->key_row > 0) {
    data->key_row--;
  } else if (down) {
    if (on_zero) {
      data->focus = Rooms_Focus_PILLS;
      data->pill_idx = Rooms_JoinPill_JOIN;
    } else {
      data->key_row++;
      if (data->key_row == KEYPAD_ROWS - 1) {
        data->key_col = 1;
      }
    }
  } else if (left && !on_zero) {
    data->key_col = (data->key_col + KEYPAD_COLUMNS - 1) % KEYPAD_COLUMNS;
  } else if (right && !on_zero) {
    data->key_col = (data->key_col + 1) % KEYPAD_COLUMNS;
  } else if (downInputs & HSD_BUTTON_A) {
    if (data->entry_len == ENTRY_DIGITS) {
      SFX_PlayCommon(CommonSound_ERROR);
      return false;
    }
    data->entry[data->entry_len++] = on_zero ? 0 : data->key_row * KEYPAD_COLUMNS + data->key_col + 1;
    SFX_PlayCommon(CommonSound_ACCEPT);

    // With every digit in, go straight to Join
    if (data->entry_len == ENTRY_DIGITS) {
      data->focus = Rooms_Focus_PILLS;
      data->pill_idx = Rooms_JoinPill_JOIN;
    }
    return true;
  } else if (downInputs & HSD_BUTTON_B) {
    if (data->entry_len == 0) {
      CloseForm();
    } else {
      data->entry_len--;
    }
    SFX_PlayCommon(CommonSound_BACK);
    return true;
  } else {
    return false;
  }

  SFX_PlayCommon(CommonSound_NEXT);
  return true;
}

// Up and down pick a setting, left and right change it, and down from the last one
// moves to the Create and Cancel pills. B closes the form
u8 HandleCreateInputs(u8 up, u8 down, u8 left, u8 right, u64 downInputs) {
  int count = settings[data->setting_idx].count;
  int *choice = &data->choices[data->setting_idx];

  if (up && data->setting_idx > 0) {
    data->setting_idx--;
  } else if (down) {
    if (data->setting_idx < CREATE_SETTINGS - 1) {
      data->setting_idx++;
    } else {
      data->focus = Rooms_Focus_PILLS;
      data->pill_idx = Rooms_CreatePill_CREATE;
    }
  } else if (left) {
    *choice = (*choice + count - 1) % count;
  } else if (right) {
    *choice = (*choice + 1) % count;
  } else if (downInputs & HSD_BUTTON_B) {
    SFX_PlayCommon(CommonSound_BACK);
    CloseForm();
    return true;
  } else {
    return false;
  }

  SFX_PlayCommon(CommonSound_NEXT);
  return true;
}

void CObjThink(GOBJ *gobj) {
  COBJ *cobj = gobj->hsd_object;

  if (!CObj_SetCurrent(cobj)) {
    return;
  }

  // The trophy screen's navy
  CObj_SetEraseColor(0, 0, 25, 255);
  CObj_EraseScreen(cobj, 1, 0, 1);
  CObj_RenderGXLinks(gobj, 7);
  CObj_EndCurrent();
}

void TextCObjThink(GOBJ *gobj) {
  COBJ *cobj = gobj->hsd_object;

  if (!CObj_SetCurrent(cobj)) {
    return;
  }

  CObj_RenderGXLinks(gobj, 7);
  CObj_EndCurrent();
}

void InputsThink(GOBJ *gobj) {
  u8 port = R13_U8(-0x5108);
  u64 downInputs = Pad_GetDown(port);
  u64 scrollInputs = Pad_GetRapidHeld(port);

  u8 up = (scrollInputs & (HSD_BUTTON_UP | HSD_BUTTON_DPAD_UP)) != 0;
  u8 down = (scrollInputs & (HSD_BUTTON_DOWN | HSD_BUTTON_DPAD_DOWN)) != 0;
  u8 left = (scrollInputs & (HSD_BUTTON_LEFT | HSD_BUTTON_DPAD_LEFT)) != 0;
  u8 right = (scrollInputs & (HSD_BUTTON_RIGHT | HSD_BUTTON_DPAD_RIGHT)) != 0;

  u8 changed = false;

  if (data->screen == Rooms_Screen_JOIN && data->focus == Rooms_Focus_LIST) {
    changed = HandleKeypadInputs(up, down, left, right, downInputs);
  } else if (data->screen == Rooms_Screen_CREATE && data->focus == Rooms_Focus_LIST) {
    changed = HandleCreateInputs(up, down, left, right, downInputs);
  } else if (data->focus == Rooms_Focus_LIST) {
    if (up && data->row_idx > 0) {
      data->row_idx--;
      SFX_PlayCommon(CommonSound_NEXT);
      changed = true;
    } else if (down) {
      if (data->row_idx < data->visible_count - 1) {
        data->row_idx++;
      } else {
        data->focus = Rooms_Focus_PILLS;
      }
      SFX_PlayCommon(CommonSound_NEXT);
      changed = true;
    } else if (downInputs & HSD_BUTTON_A) {
      // Joining a listed room is not built yet
      PlayAlert();
    } else if (downInputs & HSD_BUTTON_B) {
      SFX_PlayCommon(CommonSound_BACK);
      data->should_exit = true;
    }
  } else {
    if (left) {
      data->pill_idx = (data->pill_idx + PillCount() - 1) % PillCount();
      SFX_PlayCommon(CommonSound_NEXT);
      changed = true;
    } else if (right) {
      data->pill_idx = (data->pill_idx + 1) % PillCount();
      SFX_PlayCommon(CommonSound_NEXT);
      changed = true;
    } else if (up && (data->screen != Rooms_Screen_LIST || data->visible_count > 0)) {
      data->focus = Rooms_Focus_LIST;
      SFX_PlayCommon(CommonSound_NEXT);
      changed = true;
    } else if (downInputs & HSD_BUTTON_A) {
      HandlePillPress();
      changed = true;
    } else if (downInputs & HSD_BUTTON_B) {
      if (data->pill_row == Rooms_PillRow_JOIN && data->entry_len > 0) {
        // Deletes the last digit and goes back to the keypad to fix it
        data->entry_len--;
        data->focus = Rooms_Focus_LIST;
      } else if (data->pill_row == Rooms_PillRow_CREATE || data->pill_row == Rooms_PillRow_JOIN) {
        CloseForm();
      } else if (data->pill_row != Rooms_PillRow_MAIN) {
        data->pill_idx = data->pill_row == Rooms_PillRow_MODE ? Rooms_MainPill_MODE : Rooms_MainPill_REGION;
        data->pill_row = Rooms_PillRow_MAIN;
      } else {
        data->should_exit = true;
      }
      SFX_PlayCommon(CommonSound_BACK);
      changed = true;
    }
  }

  if (changed) {
    UpdateList();
    UpdatePills();
  }
}
