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

const GXColor pill_off = {110, 230, 80, 255};
const GXColor pill_on = {255, 220, 60, 255};
const GXColor pill_hover = {255, 255, 255, 255};
const GXColor pill_hover_on = {255, 255, 170, 255};

// The trophy list's text colors: black on the selected row, white on the rest
const GXColor row_color = {255, 255, 255, 255};
const GXColor row_selected_color = {0, 0, 0, 255};

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
  SetLabel(GetJoint(panel, PANEL_SIDE_LABEL_JOINT)->dobj, "RoomsSideLabel_image");

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

  GOBJ *cursor = LoadModel("ToyFigureListCursor_Top_joint", 0, 0);
  data->cursor = cursor->hsd_object;

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
  }

  return false;
}

void UpdatePills() {
  char **labels = main_labels;
  if (data->pill_row == Rooms_PillRow_MODE) {
    labels = mode_labels;
  } else if (data->pill_row == Rooms_PillRow_REGION) {
    labels = region_labels;
  }

  for (int i = 0; i < PILL_COUNT; i++) {
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

void UpdateList() {
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
      JOBJ_SetFlagsAll(data->rows[i], JOBJ_HIDDEN);
      Text_SetText(data->name_text[i], 0, "");
      Text_SetText(data->name_text[i], 1, "");
      Text_SetText(data->name_text[i], 2, "");
      Text_SetText(data->mode_text[i], 0, "");
      Text_SetText(data->size_text[i], 0, "");
      continue;
    }

    JOBJ_ClearFlagsAll(data->rows[i], JOBJ_HIDDEN);

    Rooms_Room *room = &rooms[data->visible[i]];
    Text_SetText(data->name_text[i], 0, room->code);
    Text_SetText(data->name_text[i], 1, room->host);
    Text_SetText(data->name_text[i], 2, region_labels[room->region]);
    Text_SetText(data->mode_text[i], 0, mode_labels[room->mode]);
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
    JOBJ_SetFlagsAll(data->list_end, JOBJ_HIDDEN);
  } else {
    Text_SetText(data->empty_text, 0, "");
    JOBJ_ClearFlagsAll(data->list_end, JOBJ_HIDDEN);
    SetRowPos(data->list_end, data->visible_count - 1);
  }

  if (data->focus == Rooms_Focus_LIST) {
    JOBJ_ClearFlagsAll(data->cursor, JOBJ_HIDDEN);
    SetRowPos(data->cursor, data->row_idx);
  } else {
    JOBJ_SetFlagsAll(data->cursor, JOBJ_HIDDEN);
  }
}

void PlayAlert() {
  SFX_PlayRaw(0xbc, 127, 64, 0, 0);  // Same warning sound as logging out
}

void HandlePillPress() {
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
    default:
      // Create and Join are not built yet
      PlayAlert();
      break;
  }
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

  if (data->focus == Rooms_Focus_LIST) {
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
      data->pill_idx = (data->pill_idx + PILL_COUNT - 1) % PILL_COUNT;
      SFX_PlayCommon(CommonSound_NEXT);
      changed = true;
    } else if (right) {
      data->pill_idx = (data->pill_idx + 1) % PILL_COUNT;
      SFX_PlayCommon(CommonSound_NEXT);
      changed = true;
    } else if (up && data->visible_count > 0) {
      data->focus = Rooms_Focus_LIST;
      SFX_PlayCommon(CommonSound_NEXT);
      changed = true;
    } else if (downInputs & HSD_BUTTON_A) {
      HandlePillPress();
      changed = true;
    } else if (downInputs & HSD_BUTTON_B) {
      if (data->pill_row != Rooms_PillRow_MAIN) {
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
