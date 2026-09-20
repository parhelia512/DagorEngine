// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <max.h>
#include <plugapi.h>
#include <ilayermanager.h>
#include <ilayerproperties.h>
#include <utilapi.h>
#include <bmmlib.h>
#include <stdmat.h>
#include <splshape.h>
#include <notetrck.h>
#include <modstack.h>
#include <cs/phyexp.h>
#include <meshnormalspec.h>
#include <impexp.h>
#include <format>
#include <string_view>
#include "comboBoxHelper.h"
#include "dagor.h"
#include "dagorLogWindow.h"
#include "enumnode.h"
#include "dagfmt.h"
#include "mater.h"
#include "Bones.h"
#include "iparamb2.h"
#include "ISkin.h"
#include "resource.h"
#include "debug.h"
#include "rolluppanel.h"
#include "ci.h"
#include "common.h"
#include "datablk.h"

// #define TIMER
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <vector>
#include <algorithm>
#include <ranges>
#include <string>
#include "Timer.hpp"
#ifdef TIMER
#define INTERVAL(elapsed, type) TimerInterval timerInterval(elapsed, type)
#else
#define INTERVAL(elapsed, type)
#endif
#include <string>
#include <utility>
#include <Shobjidl.h>
#include <Shlobj.h>
#include <sstream>
#include <fstream>
#include <filesystem>

bool is_default_layer(const ILayer &layer);

std::wstring fix_empty_param_values(std::wstring_view _script, std::wstring_view classname);
std::wstring normalize_param_values(std::wstring_view script, std::wstring_view classname);

#include "layout.h"

namespace fs = std::filesystem;

#define ERRMSG_DELAY 3000

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef int FaceNGr[3];

class ExportENCB;

void export_physics(FILE *file, Interface *ip);

void calc_momjs(Interface *ip);

void scale_matrix(Matrix3 &tm)
{
  float masterScale = static_cast<float>(GetSystemUnitScale(UNITS_METERS));
  tm.SetTrans(tm.GetTrans() * masterScale);
}

class ExpUtil : public UtilityObj
{
public:
  // Warning! This is enum is stored in the Max file.
  enum class ExportMode
  {
    Standard = 0,
    ObjectsAsDags = 1,
    LayersAsDags = 2,
  };

  IUtil *iu;
  Interface *ip;
  HWND hExpDag, hExpOther, hLog;

  fs::path exp_fname;
  fs::path exp_phys_fname;
  fs::path exp_instances_fname;

  int expflg;
  ExportMode exportMode;
  bool suppressPrompts;

  ExpUtil();
  void BeginEditParams(Interface *ip, IUtil *iu) override;
  void EndEditParams(Interface *ip, IUtil *iu) override;
  void DeleteThis() override {}

  void update_ui_dag(HWND hw);
  void update_ui_other(HWND hw);
  void update_ui();
  void update_tooltips();

  void set_expflg(int flg, int);

  void Init(HWND hw);

  int input_exp_fname();
  int input_exp_phys_fname();
  int input_exp_instances_fname();
  BOOL export_one_dag(const fs::path &exp_fn);
  BOOL export_one_dag_cb(ExportENCB &cb, const fs::path &exp_fn);
  BOOL export_dag();
  void exportPhysics();
  void export_instances();
  void calcMomj();
  BOOL export_dlg_proc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam);
  BOOL export_other_dlg_proc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam);
  void checkDupesAndSpaces(Tab<INode *> &node_list);
  void errorMessage(const TCHAR *msg);
  void warningMessage(const TCHAR *msg, const TCHAR *title = NULL);

private:
  void exportObjectsAsDagsInternal(const fs::path &folder, INode &node);
  void exportObjectsAsDags();
  void exportLayerAsDag(const fs::path &folder, ILayer &layer);
  void exportLayersAsDagsInternal(const fs::path &folder, ILayer &layer);
  void exportLayersAsDags();

  ToolTipExtender tooltipExtender;
};
static ExpUtil util;

class DagExport : public SceneExport
{
  int ExtCount() override { return 1; }
  const MCHAR *Ext(int) override { return _T("dag"); }
  const MCHAR *LongDesc() override { return GetString(IDS_DAGIMP_LONG); }
  const MCHAR *ShortDesc() override { return GetString(IDS_DAGIMP_SHORT); }
  const MCHAR *AuthorName() override { return GetString(IDS_AUTHOR); }
  const MCHAR *CopyrightMessage() override { return GetString(IDS_COPYRIGHT); }
  const MCHAR *OtherMessage1() override { return _T(""); }
  const MCHAR *OtherMessage2() override { return _T(""); }
  unsigned int Version() override { return 1; }

  void ShowAbout(HWND hWnd) override {}

  int DoExport(const MCHAR *name, ExpInterface *ei, Interface *i, BOOL suppressPrompts = FALSE, DWORD options = 0) override
  {
    util.exp_fname = name;
    util.ip = i;

    // Not sure what is the correct solution here.
    // In case of errors we already show a message box but if we report back IMPEXP_FAIL then there
    // will be an additional message box shown to the user with no additional information.
    // So we only report back errors if message prompts are suppressed.
    BOOL success = util.export_dag();
    return (success || !suppressPrompts) ? IMPEXP_SUCCESS : IMPEXP_FAIL;
  }
};

class DagExpCD : public ClassDesc
{
public:
  int IsPublic() override { return TRUE; }
  void *Create(BOOL loading) override { return new DagExport; }
  const TCHAR *ClassName() override { return GetString(IDS_DAGEXP); }
  const MCHAR *NonLocalizedClassName() override { return ClassName(); }
  SClass_ID SuperClassID() override { return SCENE_EXPORT_CLASS_ID; }
  Class_ID ClassID() override { return DAGEXP_CID; }
  const TCHAR *Category() override { return _T(""); }

  const TCHAR *InternalName() override { return _T("DagExporter"); }
  HINSTANCE HInstance() override { return hInstance; }
};
static DagExpCD dagexpcd;

ClassDesc *GetDAGEXPCD() { return &dagexpcd; }

// asked by the other translation units before they open a box of their own
bool are_prompts_suppressed() { return util.suppressPrompts; }

void explog(const TCHAR *s, ...)
{
  va_list ap;
  va_start(ap, s);
  DagorLogWindow::addToLog(DagorLogWindow::LogLevel::Note, s, ap);
  va_end(ap);
}

void explogWarning(const TCHAR *s, ...)
{
  va_list ap;
  va_start(ap, s);
  DagorLogWindow::addToLog(DagorLogWindow::LogLevel::Warning, s, ap);
  va_end(ap);
}

void explogError(const TCHAR *s, ...)
{
  va_list ap;
  va_start(ap, s);
  DagorLogWindow::addToLog(DagorLogWindow::LogLevel::Error, s, ap);
  va_end(ap);
}

class ExpUtilDesc : public ClassDesc
{
public:
  int IsPublic() override { return 1; }
  void *Create(BOOL loading = FALSE) override { return &util; }
  const TCHAR *ClassName() override { return GetString(IDS_EXPUTIL_NAME); }
  const MCHAR *NonLocalizedClassName() override { return ClassName(); }
  SClass_ID SuperClassID() override { return UTILITY_CLASS_ID; }
  Class_ID ClassID() override { return ExpUtil_CID; }
  const TCHAR *Category() override { return GetString(IDS_UTIL_CAT); }
  BOOL NeedsToSave() override { return TRUE; }
  IOResult Save(ISave *) override;
  IOResult Load(ILoad *) override;
};

// Warning! These ids are stored in the Max file. The gaps are ids that are no longer written.
enum
{
  CH_EXP_FNAME = 1,
  CH_EXPHIDDEN = 2,
  CH_EXPFLG = 3,
  CH_EXP_PHYS_FNAME = 9,
  CH_EXP_MODE = 10,
};

IOResult ExpUtilDesc::Save(ISave *io)
{
  ULONG nw;
  if (!util.exp_fname.empty())
  {
    io->BeginChunk(CH_EXP_FNAME);

    if (io->WriteCString(util.exp_fname.c_str()) != IO_OK)
      return IO_ERROR;
    io->EndChunk();
  }
  io->BeginChunk(CH_EXPFLG);
  if (io->Write(&util.expflg, 4, &nw) != IO_OK)
    return IO_ERROR;
  io->EndChunk();

  if (!util.exp_phys_fname.empty())
  {
    io->BeginChunk(CH_EXP_PHYS_FNAME);
    if (io->WriteCString(util.exp_phys_fname.c_str()) != IO_OK)
      return IO_ERROR;
    io->EndChunk();
  }

  io->BeginChunk(CH_EXP_MODE);
  const BYTE exportMode = (BYTE)util.exportMode;
  if (io->Write(&exportMode, 1, &nw) != IO_OK)
    return IO_ERROR;
  io->EndChunk();

  return IO_OK;
}

IOResult ExpUtilDesc::Load(ILoad *io)
{
  ULONG nr;
  TCHAR *str;
  util.expflg = EXP_DEFAULT;
  util.exportMode = ExpUtil::ExportMode::Standard;
  while (io->OpenChunk() == IO_OK)
  {
    switch (io->CurChunkID())
    {
      case CH_EXP_FNAME:
        if (io->ReadCStringChunk(&str) != IO_OK)
          return IO_ERROR;
        util.exp_fname = str;
        break;
      case CH_EXP_PHYS_FNAME:
        if (io->ReadCStringChunk(&str) != IO_OK)
          return IO_ERROR;
        util.exp_phys_fname = str;
        break;
      case CH_EXPHIDDEN: util.expflg |= EXP_HID; break;
      case CH_EXPFLG:
        if (io->Read(&util.expflg, 4, &nr) != IO_OK)
          return IO_ERROR;
        break;
      case CH_EXP_MODE:
        BYTE exportMode;
        if (io->Read(&exportMode, 1, &nr) != IO_OK)
          return IO_ERROR;
        util.exportMode = (ExpUtil::ExportMode)exportMode;
        break;
    }
    io->CloseChunk();
  }
  util.update_ui();
  return IO_OK;
}

static ExpUtilDesc utilDesc;
ClassDesc *GetExpUtilCD() { return &utilDesc; }

void ExpUtil::set_expflg(int f, int v)
{
  expflg &= ~f;
  if (v)
    expflg |= f;
}

BOOL ExpUtil::export_dlg_proc(HWND hw, UINT msg, WPARAM wpar, LPARAM lpar)
{
  switch (msg)
  {
    case WM_INITDIALOG: Init(hw); break;

    case WM_COMMAND:
    {
      WORD id = LOWORD(wpar);
      switch (id)
      {
        case IDC_EXPORT:
          if (exportMode == ExportMode::Standard)
          {
            if (input_exp_fname())
              export_dag();
          }
          else if (exportMode == ExportMode::LayersAsDags)
            exportLayersAsDags();
          else if (exportMode == ExportMode::ObjectsAsDags)
            exportObjectsAsDags();
          break;

        case IDC_EXPHIDDEN: set_expflg(EXP_HID, IsDlgButtonChecked(hw, id)); break;
        case IDC_EXPSEL: set_expflg(EXP_SEL, IsDlgButtonChecked(hw, id)); break;
        case IDC_EXPMESH: set_expflg(EXP_MESH, IsDlgButtonChecked(hw, id)); break;
        case IDC_EXP_VNORM: set_expflg(EXP_NO_VNORM, !IsDlgButtonChecked(hw, id)); break;
        case IDC_EXPLIGHT: set_expflg(EXP_LT, IsDlgButtonChecked(hw, id)); break;
        case IDC_EXPCAM: set_expflg(EXP_CAM, IsDlgButtonChecked(hw, id)); break;
        case IDC_EXPHELPER: set_expflg(EXP_HLP, IsDlgButtonChecked(hw, id)); break;
        case IDC_EXPSPLINE: set_expflg(EXP_SPLINE, IsDlgButtonChecked(hw, id)); break;
        case IDC_EXPMATER: set_expflg(EXP_MAT, IsDlgButtonChecked(hw, id)); break;
        case IDC_EXPMATEROPT: set_expflg(EXP_MATOPT, IsDlgButtonChecked(hw, id)); break;

        case IDC_EXPORT_MODE:
          if (HIWORD(wpar) == CBN_SELENDOK)
          {
            exportMode = (ExportMode)ComboBoxHelper::GetSelectedItemData((HWND)lpar, (LPARAM)ExportMode::Standard);
            update_tooltips();
          }
          break;
      }
    }
    break;
    default: return FALSE;
  }
  return TRUE;
}


BOOL ExpUtil::export_other_dlg_proc(HWND hw, UINT msg, WPARAM wpar, LPARAM lpar)
{
  static bool prevent_enchange = false;

  switch (msg)
  {
    case WM_INITDIALOG: update_ui_other(hw); break;

    case WM_COMMAND:
    {
      WORD id = LOWORD(wpar);
      switch (id)
      {
        case IDC_CALC_MOMJ: calcMomj(); break;
        case IDC_CALC_MOMJ_ON_EXPORT: set_expflg(EXP_DONT_CALC_MOMJ, !IsDlgButtonChecked(hw, id)); break;

        case IDC_SET_DAGORPATH:
        {
          TCHAR dir[MAX_PATH] = {};

          _tcsncpy_s(dir, _countof(dir), dagor_path.c_str(), _TRUNCATE);

          ip->ChooseDirectory(hw, GetString(IDS_CHOOSE_DAGOR_PATH), dir);
          if (dir[0])
          {
            set_dagor_path(dir);
            update_path_edit_control(hw, IDC_DAGORPATH, dagor_path);
          }
        }
        break;

        case IDC_DAGORPATH:
          switch (HIWORD(wpar))
          {
            case EN_SETFOCUS: DisableAccelerators(); break;
            case EN_KILLFOCUS:
              EnableAccelerators();
              set_dagor_path(get_window_text(GetDlgItem(hw, IDC_DAGORPATH)));
              break;

            case EN_CHANGE:
              if (!prevent_enchange)
              {
                const std::wstring path = get_window_text(GetDlgItem(hw, IDC_DAGORPATH));

                std::wstring new_path = drop_quotation_marks(path);
                if (path != new_path)
                {
                  Autotoggle eguard(prevent_enchange);
                  update_path_edit_control(hw, IDC_DAGORPATH, new_path);
                }
              }
              break;
          }
          break;

        case IDC_EXPORT_PHYS:
          if (input_exp_phys_fname())
            exportPhysics();
          break;

        case IDC_EXPORT_INSTANCES:
          if (input_exp_instances_fname())
            export_instances();
          break;

        default: break;
      }
    }
    break;

    default: return FALSE;
  }
  return TRUE;
}

static INT_PTR CALLBACK ExpDagDlgProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam)
{
  return util.export_dlg_proc(hw, msg, wParam, lParam);
}

static INT_PTR CALLBACK ExportOtherDlgProc(HWND hw, UINT msg, WPARAM wParam, LPARAM lParam)
{
  return util.export_other_dlg_proc(hw, msg, wParam, lParam);
}

static INT_PTR CALLBACK LogDlgProc(HWND hw, UINT msg, WPARAM wpar, LPARAM lpar)
{
  switch (msg)
  {
    case WM_COMMAND:
      switch (LOWORD(wpar))
      {
        case IDC_OPEN_LOG: DagorLogWindow::show(/*reset_position_and_size = */ true); break;

        case IDC_CLEAR_LOG: DagorLogWindow::clearLog(); break;
      }
      break;
    default: return FALSE;
  }
  return TRUE;
}

ExpUtil::ExpUtil()
{
  iu = NULL;
  ip = NULL;
  hExpDag = hExpOther = hLog = NULL;

  expflg = EXP_DEFAULT;
  exportMode = ExportMode::Standard;

  suppressPrompts = false;
}

void ExpUtil::BeginEditParams(Interface *ip, IUtil *iu)
{
  this->iu = iu;
  this->ip = ip;

  hExpDag = add_rollup_page(ip, IDD_EXPUTIL, ExpDagDlgProc, _T("DAG"), 0);
  hExpOther = add_rollup_page(ip, IDD_EXPOTHER, ExportOtherDlgProc, _T("Other"), 0, APPENDROLL_CLOSED);
  hLog = add_rollup_page(ip, IDD_LOGROLL, LogDlgProc, GetString(IDS_EXPLOG_ROLL), 0, APPENDROLL_CLOSED);
}

void ExpUtil::EndEditParams(Interface *ip, IUtil *iu)
{
  this->iu = NULL;
  this->ip = NULL;

  delete_rollup_page(ip, &hExpDag);
  delete_rollup_page(ip, &hExpOther);
  delete_rollup_page(ip, &hLog);
}

void ExpUtil::update_tooltips()
{
  HWND expmode = GetDlgItem(hExpDag, IDC_EXPORT_MODE);
  HWND exphidden = GetDlgItem(hExpDag, IDC_EXPHIDDEN);
  HWND expsel = GetDlgItem(hExpDag, IDC_EXPSEL);

  if (exportMode == ExportMode::LayersAsDags)
  {
    tooltipExtender.SetToolTip(expmode, _T("Layers are exported as separate dag files."));
    tooltipExtender.SetToolTip(exphidden,
      _T("Export hidden layers inside layers\n(if those layers should be exported based on other parameters)."));
    tooltipExtender.SetToolTip(expsel, _T("Process only current layer and children of it\n(The default layer is only exported if ")
                                       _T("it's the current layer and \"sel\" is active.)"));
    return;
  }

  if (exportMode == ExportMode::ObjectsAsDags)
  {
    tooltipExtender.SetToolTip(expmode, _T("Objects are exported as separate dag files."));
    tooltipExtender.SetToolTip(exphidden, _T("Export hidden objects"));
    tooltipExtender.SetToolTip(expsel, _T("Export only selected objects"));
    return;
  }

  tooltipExtender.SetToolTip(expmode, _T("Export one file."));
  tooltipExtender.SetToolTip(exphidden, _T("Export hidden objects"));
  tooltipExtender.SetToolTip(expsel, _T("Export only selected objects"));
}

void ExpUtil::update_ui_dag(HWND hw)
{
  if (!hw)
    return;

  HWND exportModeComboBox = GetDlgItem(hw, IDC_EXPORT_MODE);
  ComboBox_SetCurSel(exportModeComboBox, ComboBoxHelper::GetItemIndexByData(exportModeComboBox, (LPARAM)exportMode));

  CheckDlgButton(hw, IDC_EXPHIDDEN, expflg & EXP_HID);
  CheckDlgButton(hw, IDC_EXPSEL, expflg & EXP_SEL);
  CheckDlgButton(hw, IDC_EXPMESH, expflg & EXP_MESH);
  CheckDlgButton(hw, IDC_EXP_VNORM, !(expflg & EXP_NO_VNORM));
  CheckDlgButton(hw, IDC_EXPLIGHT, expflg & EXP_LT);
  CheckDlgButton(hw, IDC_EXPCAM, expflg & EXP_CAM);
  CheckDlgButton(hw, IDC_EXPHELPER, expflg & EXP_HLP);
  CheckDlgButton(hw, IDC_EXPSPLINE, expflg & EXP_SPLINE);
  CheckDlgButton(hw, IDC_EXPMATER, expflg & EXP_MAT);
  CheckDlgButton(hw, IDC_EXPMATEROPT, expflg & EXP_MATOPT);
}

void ExpUtil::update_ui_other(HWND hw)
{
  if (!hw)
    return;

  CheckDlgButton(hw, IDC_CALC_MOMJ_ON_EXPORT, !(expflg & EXP_DONT_CALC_MOMJ));
  update_path_edit_control(hw, IDC_DAGORPATH, dagor_path);
}

void ExpUtil::update_ui()
{
  update_ui_dag(hExpDag);
  update_ui_other(hExpOther);
}

void ExpUtil::Init(HWND hw)
{
  hExpDag = hw;

  HWND exportModeComboBox = GetDlgItem(hw, IDC_EXPORT_MODE);
  ComboBoxHelper::AddItemWithData(exportModeComboBox, _T("Standard"), (LPARAM)ExportMode::Standard);
  ComboBoxHelper::AddItemWithData(exportModeComboBox, _T("Objects as dags"), (LPARAM)ExportMode::ObjectsAsDags);
  ComboBoxHelper::AddItemWithData(exportModeComboBox, _T("Layers as dags"), (LPARAM)ExportMode::LayersAsDags);
  update_ui_dag(hw);

  tooltipExtender.SetToolTip(GetDlgItem(hw, IDC_EXPMESH), TSTR(_T("Export mesh")));
  tooltipExtender.SetToolTip(GetDlgItem(hw, IDC_EXPLIGHT), TSTR(_T("Export light")));
  tooltipExtender.SetToolTip(GetDlgItem(hw, IDC_EXPCAM), TSTR(_T("Export camera")));
  tooltipExtender.SetToolTip(GetDlgItem(hw, IDC_EXPHELPER), TSTR(_T("Export helper")));
  tooltipExtender.SetToolTip(GetDlgItem(hw, IDC_EXP_VNORM), TSTR(_T("Export vertex normals")));
  tooltipExtender.SetToolTip(GetDlgItem(hw, IDC_EXPMATER), TSTR(_T("Export materials")));
  tooltipExtender.SetToolTip(GetDlgItem(hw, IDC_EXPMATEROPT), TSTR(_T("Enable material optimization")));
  tooltipExtender.SetToolTip(GetDlgItem(hw, IDC_EXPSPLINE), TSTR(_T("Export splines")));
  update_tooltips();

  SendMessage(tooltipExtender.GetToolTipHWND(), TTM_SETDELAYTIME, TTDT_AUTOPOP, 32000);
}

int ExpUtil::input_exp_fname()
{
  FilterList fl;
  fl.Append(GetString(IDS_SCENE_FILES));
  fl.Append(_T("*.dag"));
  return get_save_filename(hExpDag, GetString(IDS_SAVE_SCENE_TITLE), fl, _T("dag"), exp_fname);
}

int ExpUtil::input_exp_phys_fname()
{
  FilterList fl;
  fl.Append(GetString(IDS_PHYS_FILES));
  fl.Append(_T ("*.dphys"));
  return get_save_filename(hExpOther, GetString(IDS_SAVE_PHYS_TITLE), fl, _T("dphys"), exp_phys_fname);
}

int ExpUtil::input_exp_instances_fname()
{
  FilterList fl;
  fl.Append(_T("Instances placement (*.blk)"));
  fl.Append(_T("*.blk"));
  return get_save_filename(hExpOther, _T("Save instances placement..."), fl, _T("blk"), exp_instances_fname);
}

// isolate struct Block to avoid linker confusion with struct Block in dagimp.cpp
namespace
{
struct Block
{
  int ofs;
};
} // namespace

static Tab<Block> blk;
static FILE *fileh = NULL;

static void init_blk(FILE *h)
{
  blk.SetCount(0);
  fileh = h;
}

static int begin_blk(int t)
{
  int n = 4;
  if (fwrite(&n, 4, 1, fileh) != 1)
    return 0;
  if (fwrite(&t, 4, 1, fileh) != 1)
    return 0;
  Block b;
  b.ofs = ftell(fileh);
  n = blk.Count();
  blk.Append(1, &b);
  if (blk.Count() != n + 1)
    return 0;
  return 1;
}

static int end_blk()
{
  int i = blk.Count() - 1;
  if (i < 0)
    return 0;
  int o = ftell(fileh);
  fseek(fileh, blk[i].ofs - 8, SEEK_SET);
  int n = o - blk[i].ofs + 4;
  if (fwrite(&n, 4, 1, fileh) != 1)
    return 0;
  blk.Delete(i, 1);
  fseek(fileh, o, SEEK_SET);
  return 1;
}


static inline void adjwtm(Matrix3 &tm)
{
  MRow *m = tm.GetAddr();
  for (int i = 0; i < 4; ++i)
  {
    float a = m[i][1];
    m[i][1] = m[i][2];
    m[i][2] = a;
  }
  tm.ClearIdentFlag(ROT_IDENT | SCL_IDENT);
}

struct EMat
{
  std::wstring name;
  Mtl *mtl;
  INode *node;
  DWORD wirecolor;

  EMat(std::wstring_view nm, Mtl *m, INode *n, DWORD wc) : name(nm), mtl(m), node(n), wirecolor(wc) {}
};

struct ExpMat
{
  DagMater m;
  std::wstring name, classname, script;
  Mtl *mtl;
  DWORD wirecolor;

  ExpMat() : mtl(0), wirecolor(0) {}
};

struct ExpNode
{
  Tab<ExpNode *> child;
  ExpNode *parent;
  ushort id;

  ExpNode(int i)
  {
    id = i;
    parent = NULL;
  }
  ~ExpNode()
  {
    for (int i = 0; i < child.Count(); ++i)
      if (child[i])
        delete (child[i]);
  }
  void add_child(ExpNode *n)
  {
    if (!n)
      return;
    child.Append(1, &n);
    n->parent = this;
  }
};

#ifdef TIMER
static double mtlElapsed = 0., procAddMtl = 0., dagorMatElapsed = 0.;
static int mtlCount = 0, procNodeCount = 0;
#endif

static int face_mtl_slot(MtlID face_mtl_id, int sub_mtl_num) { return face_mtl_id % sub_mtl_num; }

static bool any_explicit_normals_set(const MeshNormalSpec *normalSpec)
{
#if defined(MAX_RELEASE_R26) && MAX_RELEASE >= MAX_RELEASE_R26
  return normalSpec->AnyExplicitNormalsSet();
#else
  for (int i = 0, num = normalSpec->GetNumNormals(); i < num; ++i)
    if (normalSpec->GetNormalExplicit(i))
      return true;
  return false;
#endif
}

static bool is_blank(wchar_t c) { return c == L' ' || c == L'\t'; }

static size_t line_start(std::wstring_view script, size_t at)
{
  const size_t nl = script.rfind(L'\n', at);
  return nl == std::wstring_view::npos ? 0 : nl + 1;
}

static size_t skip_blanks(std::wstring_view script, size_t from)
{
  while (from < script.size() && is_blank(script[from]))
    ++from;
  return from;
}

static size_t skip_blanks_back(std::wstring_view script, size_t at)
{
  while (at > 0 && is_blank(script[at - 1]))
    --at;
  return at;
}

static bool is_param_start(std::wstring_view script, size_t at)
{
  const size_t lineStart = line_start(script, at);

  const size_t firstOnLine = skip_blanks(script, lineStart);
  if (script.compare(firstOnLine, 2, L"//") == 0)
    return false;

  const size_t paramStart = skip_blanks_back(script, at);
  return paramStart == lineStart || script[paramStart - 1] == L';' || script[paramStart - 1] == L'"' ||
         script[paramStart - 1] == L'\'';
}

static size_t find_param(std::wstring_view script, std::wstring_view key)
{
  size_t at = script.find(key);
  while (at != std::wstring_view::npos && !is_param_start(script, at))
    at = script.find(key, at + 1);

  return at;
}

static std::wstring_view find_quoted_value(std::wstring_view script, std::wstring_view key)
{
  const size_t at = find_param(script, key);
  if (at == std::wstring_view::npos)
    return {};

  const size_t from = at + key.size();
  const size_t to = script.find(L'"', from);
  if (to == std::wstring_view::npos)
    return {};

  return script.substr(from, to - from);
}

static std::wstring blk_escape(std::wstring_view name)
{
  std::wstring res = replace_all(std::wstring(name), L"~", L"~~");
  res = replace_all(res, L"\"", L"~\"");
  res = replace_all(res, L"\r", L"~r");
  return replace_all(res, L"\n", L"~n");
}

class ExportENCB : public ENodeCB
{
public:
  TimeValue time;
  Tab<INode *> node;
  std::vector<ExpMat> mat;
  std::vector<std::unordered_set<Mtl *>> mtls;
  Tab<int> matIDtoMatIdx;
  std::vector<std::wstring> tex;
  std::unordered_map<std::wstring, int, CaseInsensitiveHashW, CaseInsensitiveEqualW> texIndexMap;
  std::vector<EMat> matList;
  std::unordered_set<std::wstring> matNames;
  std::unordered_set<DWORD> wcset;
  int autoMatNameCounter;
  std::unordered_map<INode *, int> nodeIdMap;
  INode *nodeOrigin;
  INode *useIdentityTransformForNode;
  char nofaces;
  bool hasDegenerateTriangles;
  bool hasNoSmoothing;
  bool hasBigMeshes;
  bool hasNonDagorMaterials;
  bool hasNonDagorLights;
  bool hasSubSubMaterials;

  explicit ExportENCB(TimeValue t)
  {
    time = t;
    nofaces = 0;
    hasDegenerateTriangles = false;
    hasNoSmoothing = false;
    hasBigMeshes = false;
    hasNonDagorMaterials = false;
    hasNonDagorLights = false;
    hasSubSubMaterials = false;
    nodeOrigin = NULL;
    useIdentityTransformForNode = nullptr;
    autoMatNameCounter = 0;
  }

  int add_tex(const TCHAR *fn)
  {
    if (!fn)
      return -1;
    if (!fn[0])
      return -1;
    auto it = texIndexMap.find(std::wstring_view(fn));
    if (it != texIndexMap.end())
      return it->second;
    assert(tex.size() != 0xFFFF);
    if (tex.size() >= 0xFFFF)
      return -1;
    int idx = static_cast<int>(tex.size());
    tex.emplace_back(fn);
    texIndexMap.emplace(fn, idx);
    return idx;
  }

  bool doesScriptMatch(std::wstring_view a, std::wstring_view b)
  {
    auto split_lines = [](std::wstring_view s) {
      std::vector<std::wstring> lines;
      for (auto sub :
        s | std::views::split(std::wstring_view(L"\r\n")) | std::views::filter([](auto &&sub) { return !std::ranges::empty(sub); }))
        lines.emplace_back(sub.begin(), sub.end());
      std::ranges::sort(lines);
      return lines;
    };

    return split_lines(a) == split_lines(b);
  }

  void prepare_expmat()
  {
    for (const auto &em : matList)
    {
      if (!em.mtl)
      {
        assert(mat.size() != 0xFFFF);
        if (mat.size() >= 0xFFFF)
          return;

        ExpMat e;
        e.name = em.name;
        e.mtl = 0;
        e.wirecolor = em.wirecolor;

        e.m.flags = 0;
        e.m.amb = e.m.diff = Color(em.wirecolor);
        e.m.spec = e.m.emis = Color(0, 0, 0);
        e.m.power = 0;

        for (int i = 0; i < DAGTEXNUM; ++i)
          e.m.texid[i] = DAGBADMATID;

        mtls.push_back(std::unordered_set<Mtl *>());
        mat.push_back(std::move(e));
        continue;
      }

      if (em.mtl->NumSubMtls())
        continue;

      Class_ID cid = em.mtl->ClassID();
      if (cid == Class_ID(DMTL_CLASS_ID, 0))
      {
        INTERVAL(dagorMatElapsed, TimerIntervalType::GATHER);
        explogWarning(_T("'%s' has standard material '%s'\r\n"), em.node->GetName(), em.name.data());
        hasNonDagorMaterials = true;

        assert(mat.size() != 0xFFFF);
        if (mat.size() >= 0xFFFF)
          return;

        StdMat *m = (StdMat *)em.mtl;

        ExpMat e;
        e.name = em.name;
        e.mtl = em.mtl;
        e.wirecolor = 0;

        e.m.flags = DAG_MF_16TEX;
        if (m->GetTwoSided())
          e.m.flags |= DAG_MF_2SIDED;

        float si = m->GetSelfIllum(time);
        e.m.amb = m->GetAmbient(time) * (1 - si);
        e.m.diff = m->GetDiffuse(time) * (1 - si);
        e.m.spec = m->GetSpecular(time) * m->GetShinStr(time);
        e.m.emis = m->GetDiffuse(time) * si;
        e.m.power = powf(2.0f, m->GetShininess(time) * 10.0f) * 4.0f;

        for (int i = 0; i < DAGTEXNUM; ++i)
          e.m.texid[i] = DAGBADMATID;

        Texmap *tex = m->GetSubTexmap(ID_DI);
        if (tex)
          if (tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0))
          {
            BitmapTex *b = (BitmapTex *)tex;
            e.m.texid[0] = add_tex(make_path_rel(b->GetMapName()));
            if (e.m.texid[0] != DAGBADMATID)
            {
              e.m.amb = e.m.diff = Color(1, 1, 1) * (1 - si);
              e.m.emis = Color(1, 1, 1) * si;
            }
          }
        e.classname = L"simple";
        e.script = L"lighting=vltmap";

        mtls.push_back(std::unordered_set<Mtl *>());
        mtls.back().insert(em.mtl);
        mat.push_back(std::move(e));
        continue;
      }

      if (cid == DagorMat_CID || cid == DagorMat2_CID)
      {
        INTERVAL(dagorMatElapsed, TimerIntervalType::GATHER);

        assert(mat.size() != 0xFFFF);
        if (mat.size() >= 0xFFFF)
          return;

        IDagorMat *m = (IDagorMat *)em.mtl->GetInterface(I_DAGORMAT);
        assert(m);

        ExpMat e;
        e.name = em.name;
        e.mtl = em.mtl;
        e.wirecolor = 0;

        e.m.flags = DAG_MF_16TEX;
        if (m->get_2sided() == IDagorMat::Sides::DoubleSided)
          e.m.flags |= DAG_MF_2SIDED;

        e.m.amb = m->get_amb();
        e.m.diff = m->get_diff();
        e.m.spec = m->get_spec();
        e.m.emis = m->get_emis();
        e.m.power = m->get_power();

        for (int i = 0; i < DAGTEXNUM; ++i)
          e.m.texid[i] = DAGBADMATID;

        e.classname = m->get_classname();
        e.script = m->get_script();

        if (!isProxymatName(e.classname))
          for (int i = 0; i < DAGTEXNUM; ++i)
            e.m.texid[i] = add_tex(make_path_rel(m->get_texname(i)));

        em.mtl->ReleaseInterface(I_DAGORMAT, m);

        mtls.push_back(std::unordered_set<Mtl *>());
        mtls.back().insert(em.mtl);
        mat.push_back(std::move(e));
        continue;
      }

      explogWarning(_T("'%s' has material '%s' of unknown type\r\n"), em.node->GetName(), em.name.data());
      hasNonDagorMaterials = true;
    }
  }

  static bool isPlaceholderMat(const ExpMat &m) { return !m.mtl; }

  bool equal_dagormats(const ExpMat &mat_a, const ExpMat &mat_b)
  {
    if (isPlaceholderMat(mat_a) || isPlaceholderMat(mat_b))
      return false;

    if (mat_a.classname.empty())
      return false;

    if (mat_b.classname.empty())
      return false;

    bool is_proxy_a = isProxymatName(mat_a.classname);
    bool is_proxy_b = isProxymatName(mat_b.classname);
    if (is_proxy_a != is_proxy_b)
      return false;

    if (is_proxy_a)
      return iequal(mat_a.classname, mat_b.classname);
    else
    {
      if (!iequal(mat_a.classname, mat_b.classname))
        return false;
    }

    if ((mat_a.m.flags & DAG_MF_2SIDED) != (mat_b.m.flags & DAG_MF_2SIDED))
      return false;

    if (memcmp(&mat_a.m.diff, &mat_b.m.diff, sizeof(mat_a.m.diff)) != 0)
      return false;

    for (int i = 0; i < DAGTEXNUM; ++i)
    {
      unsigned short texid_a = mat_a.m.texid[i];
      unsigned short texid_b = mat_b.m.texid[i];

      // skip empty slot
      if (texid_a == DAGBADMATID && texid_b == DAGBADMATID)
        continue;

      // empty != non-empty
      if (texid_a == DAGBADMATID || texid_b == DAGBADMATID)
        return false;

      // FIXME special symbols
      const std::wstring tex_a = fs::path(tex[texid_a]).stem().wstring();
      const std::wstring tex_b = fs::path(tex[texid_b]).stem().wstring();
      if (!iequal(tex_a, tex_b))
        return false;
    }

    return doesScriptMatch(normalize_param_values(mat_a.script, mat_a.classname),
      normalize_param_values(mat_b.script, mat_b.classname));
  }

  void optimize_materials(Tab<bool> &matUsed)
  {
    for (size_t i = 0; i < mat.size(); ++i)
    {
      const auto &m = mat[i];
      if (!matUsed[i])
        continue;

      for (size_t j = i + 1; j < mat.size(); ++j)
      {
        const auto &em = mat[j];
        if (!matUsed[j])
          continue;

        if (equal_dagormats(em, m))
        {
          mtls[i].insert(em.mtl);
          matUsed[j] = false;
        }
      }
    }
  }

  std::wstring makeUniqueMatName()
  {
    for (;;)
    {
      std::wstring name = std::format(_T("autoNamedMat_{}"), autoMatNameCounter++);
      if (matNames.find(name) == matNames.end())
        return name;
    }
  }

  void add_mtl(INode *node, Mtl *mtl, int subm = 0, DWORD wirecolor = 0xFFFFFF)
  {
#ifdef TIMER
    ++mtlCount;
#endif

    std::wstring mtl_name;

    if (mtl)
    {
      mtl_name = mtl->GetName().data();
      if (mtl_name.empty())
        mtl_name = makeUniqueMatName();

      int num_sub_mtls = mtl->NumSubMtls();
      if (num_sub_mtls && subm)
      {
        explogWarning(_T("'%s' has Multi/Sub-Object material '%s' in Multi/Sub-Object material\r\n"), node->GetName(),
          mtl_name.data());
        hasSubSubMaterials = true;
        return;
      }

      for (int i = 0; i < num_sub_mtls; ++i)
        if (Mtl *sub_mtl = mtl->GetSubMtl(i))
          add_mtl(node, sub_mtl, true);

      auto it = std::ranges::find_if(matList, [mtl](EMat &em) { return em.mtl == mtl; });
      if (it != matList.end())
        return;
    }
    else // mtl == 0
    {
      if (wcset.find(wirecolor) != wcset.end())
        return;

      mtl_name = makeUniqueMatName();
      wcset.emplace(wirecolor);
    }

    matList.emplace_back(EMat(mtl_name, mtl, node, wirecolor));
    matNames.insert(mtl_name);
  }

  int proc(INode *n) override
  {
    if (!n)
      return ECB_CONT;
    if (iequal(n->GetName(), L"ORIGIN"))
    {
      if ((util.expflg & EXP_SEL) && !n->Selected())
      {
        explog(_T( "skip non-selected origin\r\n" ));
        return ECB_CONT;
      }
      if (!(util.expflg & EXP_HID) && n->IsNodeHidden())
      {
        explog(_T( "skip hidden origin\r\n"));
        return ECB_CONT;
      }

      if (!nodeOrigin)
        explog(_T( "found origin node\r\n"));
      else
        explog(_T( "duplicate origin node found, use last\r\n"));
      nodeOrigin = n;
      return ECB_CONT; // don't export origin itself
    }

    if (!(util.expflg & EXP_HID))
    {
      if (n->IsNodeHidden())
        return ECB_CONT;
    }
    if (util.expflg & EXP_SEL)
      if (!n->Selected())
        return ECB_CONT;
    {
      INTERVAL(mtlElapsed, TimerIntervalType::ACC);
#ifdef TIMER
      ++procNodeCount;
#endif
      if ((util.expflg & EXP_MAT) && !(util.expflg & EXP_OBJECTS))
      {
        INTERVAL(procAddMtl, TimerIntervalType::ACC);
        add_mtl(n, n->GetMtl(), 0, n->GetWireColor());
      }
    }

    if (!(util.expflg & EXP_OBJECTS))
      return ECB_CONT;

    Object *obj = n->EvalWorldState(time).obj;
    if (obj)
    {
      SClass_ID scid = obj->SuperClassID();
      Class_ID cid = obj->ClassID();

      if (scid == GEOMOBJECT_CLASS_ID && cid == Class_ID(TARGET_CLASS_ID, 0) && !(util.expflg & EXP_CAM))
        return ECB_CONT;

      if (cid == Dummy_CID)
        obj = NULL;
      else if (scid == LIGHT_CLASS_ID)
      {
        if (!(util.expflg & EXP_LT))
          if (!n->NumberOfChildren())
            return ECB_CONT;
      }
    }
    if (!obj)
    {
      if (!(util.expflg & EXP_HLP))
        if (!n->NumberOfChildren())
          return ECB_CONT;
    }

    if (n->Renderable() && !n->GetMtl() && !n->IsGroupHead())
    {
      explogWarning(_T( "'%s' has no material\r\n"), n->GetName());
      hasNonDagorMaterials = true;
    }
    {
      INTERVAL(mtlElapsed, TimerIntervalType::ACC);
      if (util.expflg & EXP_MAT)
      {
        INTERVAL(procAddMtl, TimerIntervalType::ACC);
        add_mtl(n, n->GetMtl(), 0, n->GetWireColor());
      }
    }

    assert(node.Count() != 0xFFFF);
    if (node.Count() >= 0xFFFF)
      return ECB_CONT;
    nodeIdMap[n] = node.Count();
    node.Append(1, &n);
    return ECB_CONT;
  }

  int getnodeid(INode *n)
  {
    if (!n)
      return -1;
    auto it = nodeIdMap.find(n);
    return it != nodeIdMap.end() ? it->second : -1;
  }
  int getPlaceholderMatId(DWORD wc)
  {
    for (int i = 0; i < (int)mat.size(); ++i)
      if (isPlaceholderMat(mat[i]) && mat[i].wirecolor == wc)
        return i;
    return -1;
  }

  int getmatid(Mtl *m, DWORD wc = 0xFFFFFF)
  {
    if (!m)
      return getPlaceholderMatId(wc);
    for (int i = 0; i < mtls.size(); ++i)
      if (mtls[i].find(m) != mtls[i].end())
        return i;
    return -1;
  }

  int getusedmatid(Mtl *m, DWORD wc = 0xFFFFFF)
  {
    if (!m)
      return getPlaceholderMatId(wc);
    for (int i = 0; i < (int)mat.size(); ++i)
      if (mat[i].mtl == m)
        return i;
    return -1;
  }

  static MtlID resolve_face_mtl(MtlID face_mtl_id, const Tab<int> &sub_mtl_id_lut, int single_mtl_idx)
  {
    if (sub_mtl_id_lut.Count())
      return (MtlID)sub_mtl_id_lut[face_mtl_slot(face_mtl_id, sub_mtl_id_lut.Count())];

    return single_mtl_idx >= 0 ? (MtlID)single_mtl_idx : face_mtl_id;
  }

#define wr(p, l)                   \
  {                                \
    if (l > 0)                     \
      if (fwrite(p, l, 1, h) != 1) \
        return 0;                  \
  }

#define bblk(id)        \
  {                     \
    if (!begin_blk(id)) \
      return 0;         \
  }
#define eblk        \
  {                 \
    if (!end_blk()) \
      return 0;     \
  }

  bool checkDegenerateTriangle(INode *node, Matrix3 &applied_transform, unsigned int index1, unsigned int index2, unsigned int index3,
    Point3 &vertex1, Point3 &vertex2, Point3 &vertex3);

  bool useMOpt() const { return (util.expflg & EXP_MATOPT) && (util.expflg & EXP_MESH); }

  std::wstring_view findMergeSurvivorName(std::wstring_view named) const
  {
    for (int i = 0; i < (int)mtls.size(); ++i)
    {
      if (matIDtoMatIdx[i] < 0)
        continue;

      if (named == mat[i].name)
        return {};

      if (std::ranges::any_of(mtls[i], [named](Mtl *m) { return m && named == m->GetName().data(); }))
        return mat[i].name;
    }

    return {};
  }

  void rewriteApexInteriorMaterial(std::wstring &script) const
  {
    static constexpr std::wstring_view key = L"apex_interior_material:t=\"";

    const std::wstring_view named = find_quoted_value(script, key);
    if (named.empty())
      return;

    const std::wstring_view survivor = findMergeSurvivorName(named);
    if (!survivor.empty())
      script.replace(named.data() - script.data(), named.size(), blk_escape(survivor));
  }

  int save_node(ExpNode *enod, FILE *h)
  {

    float masterScale = static_cast<float>(GetSystemUnitScale(UNITS_METERS));

    Matrix3 originTm;
    if (nodeOrigin)
    {
      originTm = get_scaled_stretch_node_tm(nodeOrigin, time);
      adjwtm(originTm);
    }
    else
      originTm.IdentityMatrix();

    bblk(DAG_NODE);
    INode *n = node[enod->id];
    INode *pnode = NULL;
    {
      bblk(DAG_NODE_DATA);
      DagNodeData d;
      uint pid = enod->parent->id;
      if (pid < node.Count())
        pnode = node[pid];
      d.id = enod->id;
      d.cnum = enod->child.Count();
      d.flg = 0;
      if (n->Renderable())
        d.flg |= DAG_NF_RENDERABLE;
      if (n->CastShadows())
        d.flg |= DAG_NF_CASTSHADOW;
      if (n->RcvShadows())
        d.flg |= DAG_NF_RCVSHADOW;
      const ObjectState &os = n->EvalWorldState(time);
      if (os.obj && os.obj->SuperClassID() == LIGHT_CLASS_ID)
      {
        LightObject *o = static_cast<LightObject *>(os.obj);
        if (o->GetShadow())
          d.flg |= DAG_NF_CASTSHADOW;
        else
          d.flg &= ~DAG_NF_CASTSHADOW;
      }
      wr(&d, sizeof(d));
      const TCHAR *nm = n->GetName();
      if (nm)
      {
        std::string nameCopy = wideToStr(nm);
        wr(nameCopy.data(), nameCopy.size());
      }
      eblk;
    }

    {
      RollupPanel::correctUserProp(n);

      TSTR s;
      n->GetUserPropBuffer(s);
      std::wstring trimmed = trim_params(s.data());

      // TODO: create a check for presence of billboards

      if (useMOpt())
        rewriteApexInteriorMaterial(trimmed);

      std::string scr = wideToStr(trimmed);
      bblk(DAG_NODE_SCRIPT);
      wr(scr.c_str(), scr.length());
      eblk;
    }
    Tab<int> subMatIdLUT;
    if (util.expflg & EXP_MAT)
    {
      Mtl *mtl = n->GetMtl();
      int num = 0;
      if (mtl)
        num = mtl->NumSubMtls();
      if (num)
      {
        Tab<int> uniqueMatIdx;
        subMatIdLUT.SetCount(num);
        // a slot we cannot export points at the first one we can
        for (int j = 0; j < num; ++j)
          subMatIdLUT[j] = 0;

        for (int j = 0; j < num; ++j)
        {
          Mtl *sub_mtl = mtl->GetSubMtl(j);
          if (!sub_mtl)
            continue;

          int id = getmatid(sub_mtl);
          if (id < 0 || matIDtoMatIdx[id] < 0)
            continue;

          bool found = false;
          for (int x = 0; x < uniqueMatIdx.Count(); ++x)
            if (uniqueMatIdx[x] == matIDtoMatIdx[id])
            {
              found = true;
              subMatIdLUT[j] = x;
              break;
            }
          if (!found)
          {
            uniqueMatIdx.Append(1, &matIDtoMatIdx[id]);
            subMatIdLUT[j] = uniqueMatIdx.Count() - 1;
          }
        }

        if (uniqueMatIdx.Count())
        {
          bblk(DAG_NODE_MATER);
          for (int x = 0; x < uniqueMatIdx.Count(); ++x)
            wr(&uniqueMatIdx[x], 2);
          eblk;
        }
        else
          subMatIdLUT.SetCount(0);
      }
      else
      {
        num = getmatid(mtl, n->GetWireColor());
        if (num >= 0)
          num = matIDtoMatIdx[num];
        if (num >= 0)
        {
          bblk(DAG_NODE_MATER);
          wr(&num, 2);
          eblk;
        }
      }
    }
    {
      Matrix3 ntm = get_scaled_stretch_node_tm(n, time);
      adjwtm(ntm);
      bblk(DAG_NODE_TM);
      Matrix3 ptm;
      if (n == useIdentityTransformForNode)
      {
        ntm.IdentityMatrix();
        adjwtm(ntm);

        ptm.IdentityMatrix();
      }
      else if (pnode)
      {
        ptm = get_scaled_stretch_node_tm(pnode, time);
        if (!pnode->IsRootNode())
          adjwtm(ptm);
      }
      else if (nodeOrigin)
      {
        ptm = originTm;
      }
      else
        ptm.IdentityMatrix();
      ptm = ntm * Inverse(ptm);
      wr(ptm.GetAddr(), 4 * 3 * 4);
      eblk;
    }
    const ObjectState &os = n->EvalWorldState(time);
    if (os.obj)
    {
      if (os.obj->ClassID() == Dummy_CID)
        ; // no-op
      else if ((util.expflg & EXP_LT) && os.obj->SuperClassID() == LIGHT_CLASS_ID)
      {
        GenLight *o = dynamic_cast<GenLight *>(os.obj);
        if (o)
        {
          DagLight d;
          DagLight2 d2;
          Color col = o->GetRGBColor(time) * o->GetIntensity(time);
          d.r = col.r;
          d.g = col.g;
          d.b = col.b;
          d.drad = o->GetDecayRadius(time);
          d.range = o->GetAtten(time, ATTEN_END);
          d.decay = o->GetDecayType();
          d2.mult = o->GetIntensity(time);
          d2.falloff = o->GetFallsize(time);
          d2.hotspot = o->GetHotspot(time);
          switch (o->Type())
          {
            case TSPOT_LIGHT:
            case FSPOT_LIGHT: d2.type = DAG_LIGHT_SPOT; break;
            case DIR_LIGHT:
            case TDIR_LIGHT: d2.type = DAG_LIGHT_DIR; break;
            default: d2.type = DAG_LIGHT_OMNI; break;
          }
          bblk(DAG_NODE_OBJ);
          bblk(DAG_OBJ_LIGHT);
          wr(&d, sizeof(d));
          wr(&d2, sizeof(d2));
          eblk;
          eblk;
        }
        else
        {
          hasNonDagorLights = true;
          DagorLogWindow::show();
          DagorLogWindow::addToLog(DagorLogWindow::LogLevel::Warning, _T("The light source '%s' is ignored.\r\n"), n->GetName());
        }
      }
      else if ((util.expflg & EXP_SPLINE) && os.obj->SuperClassID() == SHAPE_CLASS_ID)
      {
        ShapeObject *shobj = (ShapeObject *)os.obj;
        if (shobj->CanMakeBezier())
        {
          Matrix3 otm;
          otm = get_scaled_object_tm(n, time);
          otm = otm * Inverse(get_scaled_stretch_node_tm(n, time));
          BezierShape shp;
          shobj->MakeBezier(time, shp);
          bblk(DAG_NODE_OBJ);
          bblk(DAG_OBJ_SPLINES);
          int ns = shp.SplineCount();
          wr(&ns, 4);
          for (int si = 0; si < ns; ++si)
          {
            Spline3D &s = *shp.GetSpline(si);
            assert(&s);
            char flg = s.Closed() ? DAG_SPLINE_CLOSED : 0;
            wr(&flg, 1);
            int nk = s.KnotCount();
            wr(&nk, 4);
            for (int ki = 0; ki < nk; ++ki)
            {
              char kt;
              switch (s.GetKnotType(ki))
              {
                case KTYPE_AUTO: kt = 0; break;
                case KTYPE_BEZIER: kt = DAG_SKNOT_BEZIER; break;
                case KTYPE_CORNER: kt = DAG_SKNOT_CORNER; break;
                case KTYPE_BEZIER_CORNER: kt = DAG_SKNOT_BEZIER | DAG_SKNOT_CORNER; break;
                default: kt = DAG_SKNOT_BEZIER | DAG_SKNOT_CORNER; break;
              }
              wr(&kt, 1);
              Point3 p;
              p = (s.GetVert(ki * 3) * masterScale) * otm;
              wr(&p, 3 * 4);
              p = (s.GetVert(ki * 3 + 1) * masterScale) * otm;
              wr(&p, 3 * 4);
              p = (s.GetVert(ki * 3 + 2) * masterScale) * otm;
              wr(&p, 3 * 4);
            }
          }
          eblk;
          eblk;
        }
      }
      else if ((util.expflg & EXP_MESH) && os.obj->SuperClassID() == GEOMOBJECT_CLASS_ID &&
               os.obj->CanConvertToType(Class_ID(TRIOBJ_CLASS_ID, 0)))
      {
        int modon = 0, numvert = 0;
        bool skinmod = false;
        bool physiqueMod = false;
        bool morpherMod = false;
        struct RestoreModOnExit
        {
          INode *n;
          bool restore_prop_WSM, restore_obj_ref;

          RestoreModOnExit(INode *n_) : n(n_), restore_prop_WSM(false), restore_obj_ref(false) {}
          ~RestoreModOnExit()
          {
            if (restore_prop_WSM)
            {
              IDerivedObject &der = *(IDerivedObject *)n->GetProperty(PROPID_HAS_WSM);
              der.GetModifier(der.NumModifiers() - 1)->EnableMod();
            }
            else if (restore_obj_ref)
            {
              IDerivedObject &der = *(IDerivedObject *)n->GetObjectRef();
              der.GetModifier(der.NumModifiers() - 1)->EnableMod();
            }
          }
        } restore_mod(n);

        if (n->GetProperty(PROPID_HAS_WSM))
        {
          IDerivedObject &der = *(IDerivedObject *)n->GetProperty(PROPID_HAS_WSM);
          if (der.NumModifiers() >= 1)
          {
            Modifier *mod = der.GetModifier(der.NumModifiers() - 1);
            if (mod->ClassID() == BONESMOD_CID)
            {
              modon = mod->IsEnabled();
              mod->DisableMod();
              restore_mod.restore_prop_WSM = modon;
            }
          }
        }

        if (!modon)
        {
          Object *obj = n->GetObjectRef();
          if (obj)
            if (obj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
            {
              IDerivedObject &der = *(IDerivedObject *)obj;
              if (der.NumModifiers() >= 1)
              {
                Modifier *mod = der.GetModifier(der.NumModifiers() - 1);
                assert(mod);

                if (mod->ClassID() == Class_ID(0x17bb6854, 0xa5cba2a3))
                {
                  explog(_T( "%s: Morpher mod!\r\n"), n->GetName());
                  morpherMod = true;
                  modon = mod->IsEnabled();
                  mod->DisableMod();
                  restore_mod.restore_obj_ref = modon;
                }
                else if (mod->GetInterface(I_SKIN))
                {
                  explog(_T( "%s: skin mod!\r\n"), n->GetName());
                  skinmod = true;
                  modon = mod->IsEnabled();
                  mod->DisableMod();
                  restore_mod.restore_obj_ref = modon;
                }
                else if (mod->ClassID() == Class_ID(PHYSIQUE_CLASS_ID_A, PHYSIQUE_CLASS_ID_B))
                {
                  explog(_T( "%s: Physique mod!\r\n"), n->GetName());
                  physiqueMod = true;
                  modon = mod->IsEnabled();
                  mod->DisableMod();
                  restore_mod.restore_obj_ref = modon;
                }
              }
            }
        }

        TriObject *tri = (TriObject *)n->EvalWorldState(time).obj->ConvertToType(time, Class_ID(TRIOBJ_CLASS_ID, 0));
        if (tri)
        {
          bblk(DAG_NODE_OBJ);
          Mesh m = tri->mesh;

          if (tri != os.obj)
            tri->DeleteMe();
          Matrix3 otm;
          int notSmoothed = 0;
          ;
          mesh_face_sel(m).ClearAll();

          for (int i = 0; i < m.numFaces; ++i)
          {
            if (!m.faces[i].smGroup)
            {
              mesh_face_sel(m).Set(i);
              notSmoothed++;
            }
          }

          if (notSmoothed > 0)
          {
            explogWarning(_T( "Object '%s' has no smoothing on %d faces!\r\n" ), n->GetName(), notSmoothed);
            m.AutoSmooth(25.0f * 3.1415926f / 180.0f, TRUE);
            hasNoSmoothing |= true;
            mesh_face_sel(m).ClearAll();
          }
          int v1 = 2, v2 = 1;
          otm = get_scaled_object_tm(n, time);
          if (otm.Parity())
          {
            int a = v1;
            v1 = v2;
            v2 = a;
          }
          otm = otm * Inverse(get_scaled_stretch_node_tm(n, time));
          numvert = m.numVerts;
          for (int i = 0; i < m.numVerts; ++i)
            m.verts[i] = (m.verts[i] * masterScale) * otm;

          if (m.numFaces == 0 && n->Renderable())
          {
            explogWarning(_T( "Renderable object '%s' has 0 faces!\r\n"), n->GetName());
            nofaces = 1;
          }

          int maxntv = 0;
          for (int i = 0; i < MAX_MESHMAPS; ++i)
            if (m.mapSupport(i))
              if (m.mapFaces(i))
              {
                int ntv = m.getNumMapVerts(i);
                if (ntv > maxntv)
                  maxntv = ntv;
              }
          Mtl *mtl = n->GetMtl();
          const int subMtlNum = mtl ? mtl->NumSubMtls() : 0;
          int singleMatIdx = -1;
          if (mtl && !subMtlNum)
            if (int id = getmatid(mtl); id >= 0)
              singleMatIdx = (MtlID)matIDtoMatIdx[id];

          if (m.numVerts >= 0x10000 || m.numFaces >= 0x10000 || maxntv >= 0x10000)
          {
            hasBigMeshes = true;

            bblk(DAG_OBJ_BIGMESH);
            wr(&m.numVerts, 4);
            for (int i = 0; i < m.numVerts; ++i)
              wr(&m.verts[i], 12);
            wr(&m.numFaces, 4);
            for (int i = 0; i < m.numFaces; ++i)
            {
              DagBigFace f;
              f.v[0] = m.faces[i].v[0];
              f.v[v1] = m.faces[i].v[1];
              f.v[v2] = m.faces[i].v[2];
              f.smgr = m.faces[i].smGroup;
              f.mat = resolve_face_mtl(m.faces[i].getMatID(), subMatIdLUT, singleMatIdx);
              wr(&f, sizeof(f));

              hasDegenerateTriangles |= checkDegenerateTriangle(n, otm, m.faces[i].v[0], m.faces[i].v[1], m.faces[i].v[2],
                m.verts[m.faces[i].v[0]], m.verts[m.faces[i].v[1]], m.verts[m.faces[i].v[2]]);
            }
            unsigned char numch = 0;
            int ch;
            for (ch = 0; ch < MAX_MESHMAPS; ++ch)
              if (m.mapSupport(ch))
                if (m.mapFaces(ch))
                  if (m.getNumMapVerts(ch) > 0)
                    ++numch;
            wr(&numch, 1);
            for (ch = 0; ch < MAX_MESHMAPS; ++ch)
            {
              if (!m.mapSupport(ch))
                continue;
              TVFace *tf = m.mapFaces(ch);
              if (!tf)
                continue;
              int ntv = m.getNumMapVerts(ch);
              if (ntv <= 0)
                continue;
              wr(&ntv, 4);
              wr(ch == 0 ? "\3" : "\2", 1);
              wr(&ch, 1);
              Point3 *tv = m.mapVerts(ch);
              for (int i = 0; i < ntv; ++i)
                wr(&tv[i], ch == 0 ? 12 : 8);
              for (int i = 0; i < m.numFaces; ++i)
              {
                DagBigTFace f;
                f.t[0] = tf[i].t[0];
                f.t[v1] = tf[i].t[1];
                f.t[v2] = tf[i].t[2];
                wr(&f, sizeof(f));
              }
            }
            eblk;
          }
          else
          {
            bblk(DAG_OBJ_MESH);
            wr(&m.numVerts, 2);
            int i;
            for (i = 0; i < m.numVerts; ++i)
              wr(&m.verts[i], 12);
            wr(&m.numFaces, 2);
            for (i = 0; i < m.numFaces; ++i)
            {
              DagFace f;
              f.v[0] = (unsigned short)m.faces[i].v[0];
              f.v[v1] = (unsigned short)m.faces[i].v[1];
              f.v[v2] = (unsigned short)m.faces[i].v[2];
              f.smgr = m.faces[i].smGroup;
              f.mat = resolve_face_mtl(m.faces[i].getMatID(), subMatIdLUT, singleMatIdx);
              wr(&f, sizeof(f));

              hasDegenerateTriangles |= checkDegenerateTriangle(n, otm, m.faces[i].v[0], m.faces[i].v[1], m.faces[i].v[2],
                m.verts[m.faces[i].v[0]], m.verts[m.faces[i].v[1]], m.verts[m.faces[i].v[2]]);
            }
            unsigned char numch = 0;
            int ch;
            for (ch = 0; ch < MAX_MESHMAPS; ++ch)
              if (m.mapSupport(ch))
                if (m.mapFaces(ch))
                  if (m.getNumMapVerts(ch) > 0)
                    ++numch;
            wr(&numch, 1);
            for (ch = 0; ch < MAX_MESHMAPS; ++ch)
            {
              if (!m.mapSupport(ch))
                continue;
              TVFace *tf = m.mapFaces(ch);
              if (!tf)
                continue;
              int ntv = m.getNumMapVerts(ch);
              if (ntv <= 0)
                continue;
              wr(&ntv, 2);
              wr(ch == 0 ? "\3" : "\2", 1);
              wr(&ch, 1);
              Point3 *tv = m.mapVerts(ch);
              for (i = 0; i < ntv; ++i)
                wr(&tv[i], ch == 0 ? 12 : 8);
              for (i = 0; i < m.numFaces; ++i)
              {
                DagTFace f;
                f.t[0] = (unsigned short)tf[i].t[0];
                f.t[v1] = (unsigned short)tf[i].t[1];
                f.t[v2] = (unsigned short)tf[i].t[2];
                wr(&f, sizeof(f));
              }
            }
            eblk;
          }

          bblk(DAG_OBJ_FACEFLG);
          for (int i = 0; i < m.numFaces; ++i)
          {
            Face &f = m.faces[i];
            char ef = 0;
            if (f.flags & EDGE_B)
              ef |= DAG_FACEFLG_EDGE1;
            if (v1 == 1)
            {
              if (f.flags & EDGE_A)
                ef |= DAG_FACEFLG_EDGE0;
              if (f.flags & EDGE_C)
                ef |= DAG_FACEFLG_EDGE2;
            }
            else
            {
              if (f.flags & EDGE_A)
                ef |= DAG_FACEFLG_EDGE2;
              if (f.flags & EDGE_C)
                ef |= DAG_FACEFLG_EDGE0;
            }
            if (f.flags & FACE_HIDDEN)
              ef |= DAG_FACEFLG_HIDDEN;
            wr(&ef, 1);
          }
          eblk;


          if (m.numFaces > 0)
          {

            MeshNormalSpec *normalSpec = m.GetSpecifiedNormals();
            if (normalSpec && any_explicit_normals_set(normalSpec) && !(util.expflg & EXP_NO_VNORM) &&
                normalSpec->GetNumFaces() == m.numFaces && normalSpec->GetNumNormals())
            {
              Tab<FaceNGr> fngr;
              normalSpec->CheckNormals();
              int numNormals = normalSpec->GetNumNormals();
              fngr.Resize(m.numFaces);
              fngr.SetCount(m.numFaces);
              int vid[3] = {0, v1, v2};
              for (int face = 0; face < m.numFaces; face++)
                for (int vertex = 0; vertex < 3; vertex++)
                  fngr[face][vid[vertex]] = normalSpec->Face(face).GetNormalID(vertex);

              const Matrix3 normal_tm = AffineTranspose(Inverse(otm));
              Point3 *normals = normalSpec->GetNormalArray();
              for (int i = 0; i < numNormals; ++i)
              {
                const Point3 src = normals[i];
                const Point3 n = VectorTransform(normal_tm, src);
                const float len = Length(n);
                normals[i] = (len > 0 && std::isfinite(len)) ? n / len : src;
              }

              bblk(DAG_OBJ_NORMALS);
              wr(&numNormals, 4);
              wr(normals, numNormals * 12);
              wr(fngr.Addr(0), m.numFaces * 3 * 4);
              eblk;
            }
          }
        }

        if (modon)
        {
          if (morpherMod)
          {
            IDerivedObject &der = *(IDerivedObject *)n->GetObjectRef();
            Modifier *mod = der.GetModifier(der.NumModifiers() - 1);

            bblk(DAG_OBJ_MORPH);

            unsigned char num = 0;

            for (int i = 0; i < 100; ++i)
            {
              RefTargetHandle ref = mod->GetReference(101 + i);
              if (!ref)
                continue;
              ++num;
            }

            wr(&num, 1);

            for (int i = 0; i < 100; ++i)
            {
              RefTargetHandle ref = mod->GetReference(101 + i);
              if (!ref)
                continue;

#if defined(MAX_RELEASE_R27) && MAX_RELEASE >= MAX_RELEASE_R27
              TSTR name_wide = mod->SubAnimName(1 + i, false).data();
#else
              TSTR name_wide = mod->SubAnimName(1 + i);
#endif
              std::string name = wideToStr(name_wide);

              unsigned char len;
              if (name.size() > 255)
                len = 255;
              else
                len = static_cast<unsigned char>(name.size());

              wr(&len, 1);
              wr(name.data(), len);

              unsigned short nodeId = getnodeid((INode *)ref);
              wr(&nodeId, sizeof(nodeId));
            }

            eblk;
          }
          else if (physiqueMod)
          {
            IDerivedObject &der = *(IDerivedObject *)n->GetObjectRef();
            Modifier *mod = der.GetModifier(der.NumModifiers() - 1);

            IPhysiqueExport *phyExport = (IPhysiqueExport *)mod->GetInterface(I_PHYINTERFACE);
            IPhyContextExport *contextExport = (IPhyContextExport *)phyExport->GetContextInterface(n);

            contextExport->ConvertToRigid();
            contextExport->AllowBlending();

            int numv = contextExport->GetNumberVertices();

            // get bone nodes and weights
            Tab<INode *> bones;
            Tab<float> wt;

            for (int i = 0; i < numv; ++i)
            {
              IPhyVertexExport *vtx = (IPhyVertexExport *)contextExport->GetVertexInterface(i);
              if (!vtx)
                continue;

              if (vtx->GetVertexType() == RIGID_TYPE)
              {
                // rigid case
                IPhyRigidVertex *v = (IPhyRigidVertex *)vtx;

                INode *bnode = v->GetNode();

                int j;
                for (j = 0; j < bones.Count(); ++j)
                  if (bones[j] == bnode)
                    break;

                if (j >= bones.Count())
                {
                  wt.SetCount((bones.Count() + 1) * numv);
                  memset(&wt[bones.Count() * numv], 0, sizeof(float) * numv);
                  bones.Append(1, &bnode);
                }

                wt[j * numv + i] = 1;
              }
              else if (vtx->GetVertexType() == RIGID_BLENDED_TYPE)
              {
                // blended case
                IPhyBlendedRigidVertex *v = (IPhyBlendedRigidVertex *)vtx;

                int num = v->GetNumberNodes();

                for (int wi = 0; wi < num; ++wi)
                {
                  INode *bnode = v->GetNode(wi);

                  int j;
                  for (j = 0; j < bones.Count(); ++j)
                    if (bones[j] == bnode)
                      break;

                  if (j >= bones.Count())
                  {
                    wt.SetCount((bones.Count() + 1) * numv);
                    memset(&wt[bones.Count() * numv], 0, sizeof(float) * numv);
                    bones.Append(1, &bnode);
                  }

                  wt[j * numv + i] = v->GetWeight(wi);
                }
              }

              contextExport->ReleaseVertexInterface(vtx);
            }

            // write skinning data
            if (bones.Count() > 0 && numv == numvert)
            {
              bblk(DAG_OBJ_BONES);

              unsigned short numb = bones.Count();
              wr(&numb, 2);

              for (int i = 0; i < numb; ++i)
              {
                DagBone b;
                INode *bn = bones[i];
                b.id = getnodeid(bn);
                if (b.id == 0xFFFF)
                {
                  explogError(_T( "%s: skin refers to missing bone <%s> b.id=%04X\r\n"), n->GetName(), bn ? bn->GetName() : _T(""),
                    b.id);
                  return 0;
                }
                Matrix3 tm;
                if (phyExport->GetInitNodeTM(bn, tm) != MATRIX_RETURNED)
                {
                  explogWarning(_T( "%s: no Physique init tm for %s\r\n"), n->GetName(), bn->GetName());
                  tm.IdentityMatrix();
                }

                tm = bn->GetStretchTM(time) * tm;
                scale_matrix(tm);

                adjwtm(tm);
                tm = tm * Inverse(originTm);

                memcpy(&b.tm, tm.GetAddr(), 4 * 3 * 4);
                wr(&b, sizeof(b));
              }

              wr(&numvert, 4);
              wr(&wt[0], numvert * numb * sizeof(float));

              eblk;

              explog(_T( "%s: exported Physique skinning\r\n"), n->GetName());
            }
            else
            {
              explogWarning(_T( "%s: invalid bones data\r\n"), n->GetName());
            }

            // clean-up
            phyExport->ReleaseContextInterface(contextExport);
            mod->ReleaseInterface(I_PHYINTERFACE, phyExport);
          }
          else if (!skinmod)
          {
            IDerivedObject &der = *(IDerivedObject *)n->GetProperty(PROPID_HAS_WSM);
            Modifier *mod = der.GetModifier(der.NumModifiers() - 1);
            WSMObject *wsm = (WSMObject *)mod->GetInterface(I_BONES_WARP);
            if (wsm)
            {
              Tab<Bone> *bon = (Tab<Bone> *)wsm->GetInterface(I_BONES);
              if (bon)
              {
                ModContext *mc = der.GetModContext(0);
                if (mc)
                  if (mc->localData)
                  {
                    BonesModData &md = *(BonesModData *)mc->localData;
                    int numbones = 0;
                    for (int i = 0; i < bon->Count(); ++i)
                      if ((*bon)[i].node)
                        ++numbones;
                    if (md.VN == numvert && numbones == md.BN)
                    {
                      bblk(DAG_OBJ_BONES);
                      wr(&md.BN, 2);
                      for (int i = 0; i < md.BN; ++i)
                      {
                        DagBone b;
                        INode *bn = (*bon)[md.bone[i].node].node;
                        b.id = getnodeid(bn);
                        if (b.id == 0xFFFF)
                        {
                          explogError(_T( "%s: skin refers to missing bone <%s> b.id=%04X\r\n"), n->GetName(),
                            bn ? bn->GetName() : _T(""), b.id);
                          return 0;
                        }
                        Matrix3 tm = md.bone[i].nodetm;
                        adjwtm(tm);
                        tm = tm * Inverse(originTm);
                        memcpy(&b.tm, tm.GetAddr(), 4 * 3 * 4);
                        wr(&b, sizeof(b));
                      }
                      wr(&md.VN, 4);
                      wr(&md.pt[0], md.VN * md.BN * sizeof(float));
                      eblk;
                    }
                    else
                      explogWarning(_T( "%s: invalid bones data\r\n"), n->GetName());
                  }
                wsm->ReleaseInterface(I_BONES, bon);
              }
              mod->ReleaseInterface(I_BONES_WARP, wsm);
            }
          }
          else
          {
            IDerivedObject &der = *(IDerivedObject *)n->GetObjectRef();
            Modifier *mod = der.GetModifier(der.NumModifiers() - 1);
            ISkin *skin = (ISkin *)mod->GetInterface(I_SKIN);
            if (skin)
            {
              ISkinContextData *ctx = skin->GetContextInterface(n);
              int numb = skin->GetNumBones();
              if (numb > 0 && ctx && ctx->GetNumPoints() == numvert)
              {
                bblk(DAG_OBJ_BONES);
                wr(&numb, 2);
                for (int i = 0; i < numb; ++i)
                {
                  DagBone b;
                  INode *bn = skin->GetBone(i);
                  b.id = getnodeid(bn);
                  if (b.id == 0xFFFF)
                  {
                    explogError(_T( "%s: skin refers to missing bone <%s> b.id=%04X\r\n"), n->GetName(), bn ? bn->GetName() : _T(""),
                      b.id);
                    return 0;
                  }
                  Matrix3 tm;
                  if (skin->GetBoneInitTM(bn, tm, FALSE) != SKIN_OK)
                  {
                    explogWarning(_T( "%s: no skin init tm for %s\r\n"), n->GetName(), bn->GetName());
                    tm.IdentityMatrix();
                  }

                  tm = bn->GetStretchTM(time) * tm;
                  scale_matrix(tm);

                  adjwtm(tm);
                  tm = tm * Inverse(originTm);
                  memcpy(&b.tm, tm.GetAddr(), 4 * 3 * 4);
                  wr(&b, sizeof(b));
                }
                wr(&numvert, 4);
                Tab<float> wt;
                wt.SetCount(numvert * numb);
                memset(&wt[0], 0, wt.Count() * sizeof(float));
                for (int i = 0; i < numvert; ++i)
                {
                  int nb = ctx->GetNumAssignedBones(i);
                  float sum = 0;
                  for (int j = 0; j < nb; ++j)
                  {
                    int b = ctx->GetAssignedBone(i, j);
                    assert(b >= 0 && b < numb);
                    wt[i + b * numvert] = ctx->GetBoneWeight(i, j);
                    sum += wt[i + b * numvert];
                  }

                  if (sum != 0 && fabsf(sum - 1) > 1e-5)
                    for (int j = 0; j < nb; ++j)
                    {
                      int b = ctx->GetAssignedBone(i, j);
                      wt[i + b * numvert] /= sum;
                    }
                }
                wr(&wt[0], numvert * numb * sizeof(float));
                eblk;
              }
              else
                explogWarning(_T( "%s: invalid skin bones data\r\n"), n->GetName());
            }
          }
        }

        if (tri)
          eblk;
      }
    }

    if (enod->child.Count())
    {
      bblk(DAG_NODE_CHILDREN);
      for (int i = 0; i < enod->child.Count(); ++i)
        if (!save_node(enod->child[i], h))
          return 0;
      eblk;
    }
    eblk;
    return 1;
  }

  //===============================================================================//

  int save(FILE *h, Interface *ip)
  {
    prepare_expmat();

    matIDtoMatIdx.SetCount((int)mat.size());
    for (int i = 0; i < matIDtoMatIdx.Count(); ++i)
      matIDtoMatIdx[i] = (util.expflg & EXP_MESH) ? -1 : i;

    if (util.expflg & EXP_MESH)
    {
      Tab<bool> matUsed;
      matUsed.SetCount((int)mat.size());
      for (size_t i = 0; i < mat.size(); ++i)
        matUsed[i] = false;
#ifdef TIMER
      double start = Timer::NowMs();
#endif
      for (int i = 0; i < node.Count(); ++i)
      {
        if (!node[i])
          continue;
        const ObjectState &os = node[i]->EvalWorldState(time);
        Mtl *mtl = node[i]->GetMtl();
        if (int mat_num = mtl ? mtl->NumSubMtls() : 0)
        {
          if (os.obj && os.obj->SuperClassID() == GEOMOBJECT_CLASS_ID && os.obj->CanConvertToType(Class_ID(TRIOBJ_CLASS_ID, 0)))
          {
            if (TriObject *tri = (TriObject *)os.obj->ConvertToType(time, Class_ID(TRIOBJ_CLASS_ID, 0)))
            {
              Mesh m = tri->mesh;
              if (tri != os.obj)
                tri->DeleteMe();

              // mirrors the subMatIdLUT default in save_node: a slot it cannot export points at the first one it can
              Tab<int> slotMat;
              slotMat.SetCount(mat_num);
              int firstMat = -1;
              for (int j = 0; j < mat_num; ++j)
              {
                Mtl *sub_mtl = mtl->GetSubMtl(j);
                slotMat[j] = sub_mtl ? getusedmatid(sub_mtl) : -1;
                if (firstMat < 0)
                  firstMat = slotMat[j];
              }
              for (int j = 0; j < mat_num; ++j)
                if (slotMat[j] < 0)
                  slotMat[j] = firstMat;

              if (firstMat >= 0)
                for (int j = 0; j < m.numFaces; ++j)
                  matUsed[slotMat[face_mtl_slot(m.faces[j].getMatID(), mat_num)]] = true;
            }
          }
        }
        else
        {
          int mat = getusedmatid(mtl, node[i]->GetWireColor());
          if (mat >= 0)
            matUsed[mat] = true;
        }
      }

      if (useMOpt())
        optimize_materials(matUsed);

#ifdef TIMER
      double end = Timer::NowMs();
      explog(_T("matUsed: %g ms\r\n"), end - start);
      explog(_T("add_mtl: %g ms\r\n"), mtlElapsed);
      explog(_T("add_mtl count: %d\r\n"), mtlCount);
      explog(_T("proc node mtl count: %d\r\n"), procNodeCount);
      explog(_T("proc add mtl: %g\r\n"), procAddMtl);
      explog(_T("dagorMatElapsed: %g\r\n"), dagorMatElapsed);
      explog(_T("mtls: %d mat: %d\r\n"), (int)mtls.size(), (int)mat.size());
#endif
      std::wstring unused_mtls, unused_tex;

      Tab<int> tex_remap;
      std::vector<std::wstring> new_tex;
      tex_remap.SetCount(static_cast<int>(tex.size()));
      for (int i = 0; i < tex_remap.Count(); ++i)
        tex_remap[i] = -1;

      int idx = 0;
      for (size_t i = 0; i < mat.size(); ++i)
        if (matUsed[i])
        {
          // remap textures
          for (int j = 0; j < DAGTEXNUM; j++)
            if (mat[i].m.texid[j] != DAGBADMATID)
            {
              unsigned short &texid = mat[i].m.texid[j];
              if (texid >= static_cast<unsigned short>(tex_remap.Count()))
              {
                texid = DAGBADMATID;
                continue;
              }
              int new_texid = tex_remap[texid];
              if (new_texid < 0)
              {
                new_tex.push_back(tex[texid]);
                tex_remap[texid] = new_texid = static_cast<int>(new_tex.size()) - 1;
              }
              texid = new_texid;
            }
          matIDtoMatIdx[i] = idx;
          ++idx;
        }
        else
        {
          unused_mtls += L"\r\n        '";
          unused_mtls += mat[i].name;
          unused_mtls += L"'";

          // reset unused material props
          mat[i].classname.clear();
          mat[i].script.clear();
          memset(&mat[i].m, 0, sizeof(mat[i].m));
          for (int j = 0; j < DAGTEXNUM; j++)
            mat[i].m.texid[j] = DAGBADMATID;
        }

      for (int i = 0; i < static_cast<int>(tex.size()); ++i)
        if (tex_remap[i] < 0)
        {
          unused_tex += L"\r\n        '";
          unused_tex += tex[i];
          unused_tex += L"'";
        }
      tex = std::move(new_tex);
      texIndexMap.clear();
      for (int j = 0; j < static_cast<int>(tex.size()); ++j)
        texIndexMap.emplace(tex[j], j);

      if (!unused_mtls.empty())
        explog(_T("these materials are UNUSED:%s\r\n"), unused_mtls.data());

      if (!unused_tex.empty())
        explog(_T("these textures are UNUSED:%s\r\n"), unused_tex.data());
    }

    {
      int n = DAG_ID;
      wr(&n, 4);
    }
    init_blk(h);
    bblk(DAG_ID);

    // save textures
    if (!tex.empty())
    {
      bblk(DAG_TEXTURES);

      int num = static_cast<int>(tex.size());
      if (num > 0xFFFF)
        num = 0xFFFF;

      wr(&num, 2);
      for (int i = 0; i < num; ++i)
      {
        std::string name = wideToStr(tex[i]);

        std::replace(name.begin(), name.end(), '\\', '/');
        if (name.find('/') == std::string::npos)
          name = "./" + name;

        // if the name is too long
        if (name.size() > 255) // counter must fit in 1 byte
        {
          // sacrifice the directory part
          size_t pos = name.find_last_of('/');
          if (pos != std::string::npos)
            name = name.substr(pos + 1);

          // last resort to keep the size within the limits
          name = name.substr(0, 255);
        }

        size_t l = name.size();
        wr(&l, 1);
        if (l)
          wr(name.data(), l);
      }
      eblk;
    }

    // save materials
    for (int i = 0; i < (int)mat.size(); ++i)
    {
      if (matIDtoMatIdx[i] < 0)
        continue;
      bblk(DAG_MATER);
      if (!mat[i].name.empty())
      {
        std::string s = wideToStr(mat[i].name);
        size_t l = s.length();
        if (l > 255)
          l = 255;
        wr(&l, 1);
        if (l)
          wr(s.c_str(), l);
      }
      else
        wr("\0", 1);

      wr(&mat[i].m, sizeof(DagMater));
      if (!mat[i].classname.empty())
      {
        std::string s = wideToStr(mat[i].classname);
        size_t l = s.length();
        if (l > 255)
          l = 255;
        wr(&l, 1);
        if (l)
          wr(s.c_str(), l);
      }
      else
        wr("\0", 1);

      // don't export parameters of proxymats
      if (!mat[i].script.empty() && !isProxymatName(mat[i].classname))
      {
        std::string s = wideToStr(fix_empty_param_values(mat[i].script, mat[i].classname));
        size_t l = s.length();
        if (l)
          wr(s.c_str(), l);
      }

      eblk;
    }

    // save nodes
    {
      Tab<ExpNode *> en;
      en.SetCount(node.Count());
      for (int i = 0; i < en.Count(); ++i)
      {
        en[i] = new ExpNode(i);
        assert(en[i]);
      }
      std::unique_ptr<ExpNode> root(new ExpNode(-1));
      assert(root);
      for (int i = 0; i < node.Count(); ++i)
      {
        uint pid = getnodeid(node[i]->GetParentNode());
        if (pid < node.Count())
        {
          en[pid]->add_child(en[i]);
        }
        else
        {
          root->add_child(en[i]);
        }
      }
      bblk(DAG_NODE);
      bblk(DAG_NODE_DATA);
      DagNodeData d;
      d.id = 0xFFFF;
      d.cnum = root->child.Count();
      wr(&d, sizeof(d));
      eblk;
      if (root->child.Count())
      {
        bblk(DAG_NODE_CHILDREN);
        for (int i = 0; i < root->child.Count(); ++i)
          if (!save_node(root->child[i], h))
            return 0;
        eblk;
      }
      eblk;
    }

    bblk(DAG_END);
    eblk;

    eblk;
    return 1;
  }

#undef wr
#undef bblk
#undef eblk
};


void ExpUtil::errorMessage(const TCHAR *msg)
{
  ip->DisplayTempPrompt(msg, ERRMSG_DELAY);

  if (!suppressPrompts)
    MessageBox(ip->GetMAXHWnd(), msg, _T ("Error"), MB_OK | MB_ICONSTOP);

  explogError(_T("ERROR! %s\r\n"), msg);
}

void ExpUtil::warningMessage(const TCHAR *msg, const TCHAR *title)
{
  if (title == NULL)
    title = _T("Warning");

  if (!suppressPrompts)
    MessageBox(ip->GetMAXHWnd(), msg, title, MB_OK | MB_ICONSTOP);

  explogWarning(_T("WARNING! %s\r\n"), msg);
}

static bool find_co_layers(const fs::path &fname, std::vector<std::wstring> &fnames)
{
  if (!is_dag_file(fname) || !iequal(fname.stem().extension().c_str(), _T(".lod00")))
    return false;
  const fs::path base = fname.parent_path() / fname.stem().stem();

  ILayerManager *manager = GetCOREInterface13()->GetLayerManager();
  ILayer *l = NULL;

  std::wstring matched_layer_name;
  std::wstring formatted_name;
  for (int i = 0; i < 16; i++)
  {
    formatted_name = std::format(_T("LOD{:02}"), i);
    l = manager->GetLayer(formatted_name.c_str());
    if (!l)
    {
      formatted_name = std::format(_T("lod{:02}"), i);
      l = manager->GetLayer(formatted_name.c_str());
      if (!l)
      {
        formatted_name = std::format(_T("Lod{:02}"), i);
        l = manager->GetLayer(formatted_name.c_str());
      }
    }
    if (l && l->HasObjects())
    {
      fnames.push_back(formatted_name);
      formatted_name = std::format(_T("{}.lod{:02}.dag"), base.c_str(), i);
      fnames.push_back(formatted_name);
    }
  }

  for (int i = 0; i < 16; i++)
  {
    matched_layer_name = std::format(_T("DESTR_LOD{:02}"), i);
    l = manager->GetLayer(matched_layer_name.c_str());
    if (!l)
    {
      matched_layer_name = std::format(_T("destr_lod{:02}"), i);
      l = manager->GetLayer(matched_layer_name.c_str());
      if (!l)
      {
        matched_layer_name = std::format(_T("Destr_lod{:02}"), i);
        l = manager->GetLayer(matched_layer_name.c_str());
      }
    }

    if (l && l->HasObjects())
    {
      fnames.push_back(matched_layer_name);
      formatted_name = std::format(_T("{}_destr.lod{:02}.dag"), base.c_str(), i);
      fnames.push_back(formatted_name);
    }
  }

  matched_layer_name = _T("DM");
  l = manager->GetLayer(matched_layer_name.c_str());
  if (!l)
  {
    matched_layer_name = _T("dm");
    l = manager->GetLayer(matched_layer_name.c_str());
    if (!l)
    {
      matched_layer_name = _T("Dm");
      l = manager->GetLayer(matched_layer_name.c_str());
    }
  }
  if (l && l->HasObjects())
  {
    fnames.push_back(matched_layer_name);
    formatted_name = std::format(_T("{}_dm.dag"), base.c_str());
    fnames.push_back(formatted_name);
  }

  for (int i = 0; i < 16; i++)
  {
    formatted_name = std::format(_T("DMG_LOD{:02}"), i);
    l = manager->GetLayer(formatted_name.c_str());
    if (!l)
    {
      formatted_name = std::format(_T("dmg_lod{:02}"), i);
      l = manager->GetLayer(formatted_name.c_str());
      if (!l)
      {
        formatted_name = std::format(_T("Dmg_lod{:02}"), i);
        l = manager->GetLayer(formatted_name.c_str());
      }
    }
    if (l && l->HasObjects())
    {
      fnames.push_back(formatted_name);
      formatted_name = std::format(_T("{}_dmg.lod{:02}.dag"), base.c_str(), i);
      fnames.push_back(formatted_name);
    }
  }

  for (int i = 0; i < 16; i++)
  {
    formatted_name = std::format(_T("DMG2_LOD{:02}"), i);
    l = manager->GetLayer(formatted_name.c_str());
    if (!l)
    {
      formatted_name = std::format(_T("dmg2_lod{:02}"), i);
      l = manager->GetLayer(formatted_name.c_str());
      if (!l)
      {
        formatted_name = std::format(_T("Dmg2_lod{:02}"), i);
        l = manager->GetLayer(formatted_name.c_str());
      }
    }
    if (l && l->HasObjects())
    {
      fnames.push_back(formatted_name);
      formatted_name = std::format(_T("{}_dmg2.lod{:02}.dag"), base.c_str(), i);
      fnames.push_back(formatted_name);
    }
  }

  for (int i = 0; i < 16; i++)
  {
    formatted_name = std::format(_T("EXPL_LOD{:02}"), i);
    l = manager->GetLayer(formatted_name.c_str());
    if (!l)
    {
      formatted_name = std::format(_T("expl_lod{:02}"), i);
      l = manager->GetLayer(formatted_name.c_str());
      if (!l)
      {
        formatted_name = std::format(_T("Expl_lod{:02}"), i);
        l = manager->GetLayer(formatted_name.c_str());
      }
    }
    if (l && l->HasObjects())
    {
      fnames.push_back(formatted_name);
      formatted_name = std::format(_T("{}_expl.lod{:02}.dag"), base.c_str(), i);
      fnames.push_back(formatted_name);
    }
  }

  matched_layer_name = _T("XRAY");
  l = manager->GetLayer(matched_layer_name.c_str());
  if (!l)
  {
    matched_layer_name = _T("xray");
    l = manager->GetLayer(matched_layer_name.c_str());
  }

  if (l && l->HasObjects())
  {
    fnames.push_back(matched_layer_name);
    formatted_name = std::format(_T("{}_xray.dag"), base.c_str());
    fnames.push_back(formatted_name);
  }

  if (fnames.size() / 2 > 1)
    return true;

  fnames.clear();
  return false;
}

BOOL ExpUtil::export_dag()
{
  DagorLogWindowAutoShower logWindowAutoShower(/*clear_log = */ true);

  std::vector<std::wstring> fnames;
  Tab<bool> isHidden;

  if (!find_co_layers(exp_fname, fnames))
    return export_one_dag(exp_fname);

  std::wstring buf = L"Export layers to " + std::to_wstring(fnames.size() / 2) + L" separate files?\n";
  for (size_t i = 0; i + 1 < fnames.size() && buf.size() < (7 << 10); i += 2)
  {
    const std::wstring &dst = fnames[i + 1];
    buf += fnames[i];
    buf += L" -> ";
    buf += dst.size() > 48 ? L"..." + dst.substr(dst.size() - 48) : dst;
    buf += L'\n';
  }

  if (MessageBox(GetFocus(), buf.c_str(), _T("Export layered DAGs"), MB_YESNO | MB_ICONQUESTION) != IDYES)
    return export_one_dag(exp_fname);

  ILayerManager *manager = GetCOREInterface13()->GetLayerManager();
  isHidden.SetCount(static_cast<int>(fnames.size() / 2));
  for (int i = 0; i < (int)fnames.size(); i += 2)
  {
    ILayer *l = manager->GetLayer(fnames[i].data());
    isHidden[i / 2] = l->IsHidden();
    l->Hide(true);
  }
  for (int i = 0; i < (int)fnames.size(); i += 2)
  {
    TSTR lnm(fnames[i].data());
    ILayer *l = manager->GetLayer(lnm);
    manager->SetCurrentLayer(lnm);
    l->Hide(false);

    export_one_dag(fnames[i + 1]);
    l->Hide(true);
  }
  for (int i = 0; i < (int)fnames.size(); i += 2)
  {
    if (!isHidden[i / 2])
      manager->GetLayer(fnames[i].data())->Hide(false);
  }
  manager->SetCurrentLayer();

  return true;
}

BOOL ExpUtil::export_one_dag(const fs::path &exp_fn)
{
  ExportENCB cb(ip->GetTime());
  enum_nodes(ip->GetRootNode(), &cb);

  if (!cb.node.Count())
  {
    errorMessage(_T("There are no nodes."));
    return false;
  }

  return export_one_dag_cb(cb, exp_fn);
}

BOOL ExpUtil::export_one_dag_cb(ExportENCB &cb, const fs::path &exp_fn)
{
  checkDupesAndSpaces(cb.node);

  FILE *h = _tfopen(exp_fn.c_str(), _T ("wb"));
  if (!h)
  {
    errorMessage(GetString(IDS_FILE_CREATE_ERR));
    return false;
  }
  if (!cb.save(h, ip))
  {
    errorMessage(GetString(IDS_FILE_WRITE_ERR));
    fclose(h);
    return false;
  }
  fclose(h);
  explog(_T ("%d nodes\r\n"), cb.node.Count());
  explog(_T ("%d materials\r\n"), (int)cb.mat.size());
  explog(_T ("%d textures\r\n"), (int)cb.tex.size());
  if (cb.nofaces)
  {
    warningMessage(GetString(IDS_NOFACES_NODES));
  }
  if (cb.hasDegenerateTriangles)
  {
    warningMessage(GetString(IDS_DEGENERATE_TRIANGLES));
  }

  if (cb.hasNoSmoothing)
  {
    warningMessage(GetString(IDS_NO_SMOOTHING));
  }
  if (cb.hasBigMeshes)
  {
    warningMessage(GetString(IDS_BIG_MESHES));
  }
  if (cb.hasNonDagorMaterials)
  {
    warningMessage(GetString(IDS_NON_DAGOR_MATERIALS));
  }
  if (cb.hasNonDagorLights)
  {
    warningMessage(GetString(IDS_NON_DAGOR_LIGHTS));
  }
  if (cb.hasSubSubMaterials)
  {
    warningMessage(GetString(IDS_SUB_SUB_MATERIALS));
  }
  return true;
}
static bool FolderOpenDialog(fs::path &path, HWND)
{
  path.clear();

  IFileDialog *pfd;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
  if (!SUCCEEDED(hr))
    return false;

  DWORD dwFlags;
  hr = pfd->GetOptions(&dwFlags);
  if (!SUCCEEDED(hr))
  {
    pfd->Release();
    return false;
  }
  hr = pfd->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS);
  if (!SUCCEEDED(hr))
  {
    pfd->Release();
    return false;
  }

  hr = pfd->Show(NULL);
  if (!SUCCEEDED(hr))
  {
    pfd->Release();
    return false;
  }

  IShellItem *psiResult = 0;
  hr = pfd->GetResult(&psiResult);
  if (!SUCCEEDED(hr))
  {
    pfd->Release();
    return false;
  }

  PWSTR pszFilePath = NULL;
  psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
  if (pszFilePath)
  {
    path = pszFilePath;
    CoTaskMemFree(pszFilePath);
  }
  psiResult->Release();
  pfd->Release();
  return !path.empty();
}

void ExpUtil::exportObjectsAsDagsInternal(const fs::path &folder, INode &node)
{
  const int childrenCount = node.NumberOfChildren();
  for (int i = 0; i < childrenCount; ++i)
  {
    INode *childNode = node.GetChildNode(i);

    if (((util.expflg & EXP_SEL) != 0 && childNode->Selected() == 0) || ((util.expflg & EXP_HID) == 0 && childNode->IsHidden()))
    {
      exportObjectsAsDagsInternal(folder, *childNode);
      continue;
    }

    ExportENCB cb(ip->GetTime());

    if (childNode->IsGroupHead())
      enum_nodes_by_node(childNode, &cb);
    else
      cb.proc(childNode);

    if (cb.node.Count() > 0)
    {
      // Use the node as origin instead of the scene.
      cb.useIdentityTransformForNode = childNode;

      fs::path path = folder / childNode->GetName();
      path += L".dag";

      explog(_T("Exporting node \"%s\" to file \"%s\"\r\n"), childNode->GetName(), path.c_str());
      export_one_dag_cb(cb, path);
      explog(_T("\r\n"));
    }
    else
    {
      explogWarning(_T("Skipping node '%s'. It does no match the export settings (beside \"sel\" and \"hid\").\r\n"),
        childNode->GetName());
    }

    // If this is a group then there is no need to descend further because groups are exported as a single dag.
    if (!childNode->IsGroupHead())
      exportObjectsAsDagsInternal(folder, *childNode);
  }
}

void ExpUtil::exportObjectsAsDags()
{
  DagorLogWindowAutoShower logWindowAutoShower(/*clear_log = */ true);

  fs::path folder;
  if (!FolderOpenDialog(folder, hExpDag))
    return;

  INode *rootNode = GetCOREInterface13()->GetRootNode();
  if (!rootNode)
  {
    explogError(_T("Could not get the root node.\r\n"));
    explogError(_T("Aborting export.\r\n"));
    return;
  }

  exportObjectsAsDagsInternal(folder, *rootNode);
}

void ExpUtil::exportLayerAsDag(const fs::path &folder, ILayer &layer)
{
  // "sel" only affects the layers we work with, so do not use it when processing the nodes.
  const int oldExpSel = util.expflg & EXP_SEL;
  util.expflg &= ~EXP_SEL;

  ILayerProperties *layerProperties = (ILayerProperties *)layer.GetInterface(LAYERPROPERTIES_INTERFACE);
  if (layerProperties)
  {
    ExportENCB cb(ip->GetTime());

    // ILayerProperties::Nodes returns with all nodes belonging to a layer, regardless of the hierarchy,
    // so there is no need to call enum_nodes.
    Tab<INode *> nodes;
    layerProperties->Nodes(nodes);
    for (int i = 0; i < nodes.Count(); ++i)
      cb.proc(nodes[i]);

    if (cb.node.Count() > 0)
    {
      fs::path path = folder / layer.GetName().data();
      path += L".dag";

      explog(_T("Exporting layer \"%s\" to file \"%s\"\r\n"), layer.GetName().data(), path.c_str());
      export_one_dag_cb(cb, path);
      explog(_T("\r\n"));
    }
    else
    {
      explogWarning(_T("Skipping layer '%s'. It has no nodes to export that matches the export settings.\r\n"),
        layer.GetName().data());
    }
  }
  else
  {
    explogError(_T("Cannot get the layer properties for layer '%s'.\r\n"), layer.GetName().data());
  }

  util.expflg |= oldExpSel;
}

void ExpUtil::exportLayersAsDagsInternal(const fs::path &folder, ILayer &layer)
{
  const bool matchesVisibilityFilter = (util.expflg & EXP_HID) != 0 || !layer.IsHidden(false);

  const int childLayerCount = layer.GetNumOfChildLayers();

  if (childLayerCount > 0)
  {
    if (matchesVisibilityFilter)
      explog(_T("Layer '%s' has child layer(s), it will not be exported.\r\n"), layer.GetName().data());

    for (int i = 0; i < childLayerCount; ++i)
      exportLayersAsDagsInternal(folder, *layer.GetChildLayer(i));
  }
  else if (matchesVisibilityFilter)
    exportLayerAsDag(folder, layer);
}

void ExpUtil::exportLayersAsDags()
{
  DagorLogWindowAutoShower logWindowAutoShower(/*clear_log = */ true);

  fs::path folder;
  if (!FolderOpenDialog(folder, hExpDag))
    return;

  ILayerManager *manager = GetCOREInterface13()->GetLayerManager();
  if (!manager)
  {
    explogError(_T("Could not get the layer manager.\r\n"));
    explogError(_T("Aborting export.\r\n"));
    return;
  }

  if ((util.expflg & EXP_SEL) == 0)
  {
    const int layerCount = manager->GetLayerCount();
    for (int layerIndex = 0; layerIndex < layerCount; ++layerIndex)
    {
      ILayer *layer = manager->GetLayer(layerIndex);

      // Ignore non-top level layers, we will enumerate them hierarchially.
      if (layer->GetParentLayer())
        continue;

      // The default layer itself is not exported (except when "sel" is enabled and it is the active layer), only its children.
      if (is_default_layer(*layer))
      {
        const int childLayerCount = layer->GetNumOfChildLayers();
        for (int childLayerIndex = 0; childLayerIndex < childLayerCount; ++childLayerIndex)
          exportLayersAsDagsInternal(folder, *layer->GetChildLayer(childLayerIndex));
      }
      else
        exportLayersAsDagsInternal(folder, *layer);
    }
  }
  else
  {
    ILayer *currentLayer = manager->GetCurrentLayer();
    if (!currentLayer)
    {
      explogError(_T("Could not get the current layer.\r\n"));
      explogError(_T("Aborting export.\r\n"));
      return;
    }

    exportLayersAsDagsInternal(folder, *currentLayer);
  }
}


void ExpUtil::exportPhysics()
{
  DagorLogWindowAutoShower logWindowAutoShower(/*clear_log = */ true);
  explog(_T ("Exporting physics file <%s>...\r\n"), exp_phys_fname.c_str());

  if (!(util.expflg & EXP_DONT_CALC_MOMJ))
  {
    explog(_T("Updating masses and momjs...\r\n"));
    ::calc_momjs(ip);
  }

  FILE *h = _tfopen(exp_phys_fname.c_str(), _T ("wb"));
  if (!h)
  {
    errorMessage(GetString(IDS_FILE_CREATE_ERR));
    return;
  }

  ::export_physics(h, ip);

  explog(_T ("Success!\r\n"));
  fclose(h);
}


void ExpUtil::calcMomj()
{
  DagorLogWindowAutoShower logWindowAutoShower(/*clear_log = */ true);
  explog(_T("Updating masses and momjs...\r\n"));

  ::calc_momjs(ip);

  explog(_T("Success!\r\n"));
}


void ExpUtil::checkDupesAndSpaces(Tab<INode *> &node_list)
{
  bool hasDupes = false;
  bool hasEmptyNames = false;

  std::unordered_map<std::wstring_view, bool, CaseInsensitiveHashW, CaseInsensitiveEqualW> seenNames;
  seenNames.reserve(node_list.Count());

  for (int i = 0; i < node_list.Count(); ++i)
  {
    if (!(expflg & EXP_HID) && node_list[i]->IsNodeHidden())
      continue;

    if ((expflg & EXP_SEL) && !node_list[i]->Selected())
      continue;

    const wchar_t *name = node_list[i]->GetName();

    if (name[0] == 0)
    {
      explogWarning(L"node with empty name\r\n");
      hasEmptyNames = true;
    }

    auto [it, inserted] = seenNames.emplace(name, false);
    if (!inserted)
    {
      const wchar_t *duplicateNameFmt = L"duplicate node name \"%s\"\r\n";
      if (!it->second)
      {
        explogWarning(duplicateNameFmt, it->first.data());
        it->second = true;
      }
      explogWarning(duplicateNameFmt, name);
      hasDupes = true;
    }
  }

  if (hasDupes)
  {
    ip->DisplayTempPrompt(L"There are duplicate node names.", ERRMSG_DELAY);
    warningMessage(L"There are nodes with the same names.\n See log for details.", L"Duplicate names");
  }

  if (hasEmptyNames)
  {
    ip->DisplayTempPrompt(L"There is a node with empty name.", ERRMSG_DELAY);
    warningMessage(L"There is a node with empty name.\n See log for details.", L"Empty name");
  }
}


void ExpUtil::export_instances()
{
  DagorLogWindowAutoShower logWindowAutoShower(/*clear_log = */ true);
  explog(_T("Exporting instances placement file <%s>...\r\n"), exp_instances_fname.c_str());

  ExportENCB cb(ip->GetTime());
  enum_nodes(ip->GetRootNode(), &cb);

  std::ofstream os(exp_instances_fname, std::ios::binary);
  if (!os)
  {
    errorMessage(GetString(IDS_FILE_CREATE_ERR));
    return;
  }

  // Find base ground plane.

  float planeOffsetX = 1e20f;
  float planeOffsetZ = 1e20f;
  float planeSize = 0;
  int numTexturesWidth = 0, numTexturesHeight = 0;
  bool wasFound = false;
  for (unsigned int nodeNo = 0; nodeNo < cb.node.Count(); nodeNo++)
  {
    if (!cb.node[nodeNo]->GetParentNode() || !cb.node[nodeNo]->GetParentNode()->IsRootNode())
      continue;

    if (!istarts_with(cb.node[nodeNo]->GetName(), _T("GroundPlane")))
      continue;

    float newPlaneOffsetX;
    if (!cb.node[nodeNo]->GetUserPropFloat(_T("PlaneOffsetX:r"), newPlaneOffsetX))
    {
      errorMessage(std::format(_T("'{}': invalid PlaneOffsetX:r"), cb.node[nodeNo]->GetName()).c_str());
      return;
    }

    float newPlaneOffsetZ;
    if (!cb.node[nodeNo]->GetUserPropFloat(_T("PlaneOffsetZ:r"), newPlaneOffsetZ))
    {
      errorMessage(std::format(_T("'{}': invalid PlaneOffsetZ:r"), cb.node[nodeNo]->GetName()).c_str());
      return;
    }

    if (newPlaneOffsetX <= planeOffsetX || newPlaneOffsetZ <= planeOffsetZ)
    {
      planeOffsetX = newPlaneOffsetX;
      planeOffsetZ = newPlaneOffsetZ;

      if (!cb.node[nodeNo]->GetUserPropFloat(_T("PlaneSize:r"), planeSize))
      {
        errorMessage(std::format(_T("'{}': invalid PlaneSize:r"), cb.node[nodeNo]->GetName()).c_str());
        return;
      }

      if (!cb.node[nodeNo]->GetUserPropInt(_T("numTexturesWidth:i"), numTexturesWidth))
        numTexturesWidth = 16;

      if (!cb.node[nodeNo]->GetUserPropInt(_T("numTexturesHeight:i"), numTexturesHeight))
        numTexturesHeight = 16;

      wasFound = true;
    }
  }

  if (!wasFound)
  {
    errorMessage(_T("GroundPlane not found"));
    return;
  }

  float offsetX = planeOffsetX - 0.5f * numTexturesWidth * planeSize;
  float offsetZ = planeOffsetZ - 0.5f * numTexturesHeight * planeSize;
  unsigned int planeNo =
    (unsigned int)floorf(planeOffsetZ / planeSize + 0.5f) * numTexturesWidth + (unsigned int)floorf(planeOffsetX / planeSize + 0.5f);

  os << std::format("Version:i=20070810\r\nPlaneOffsetX:r={:f}\r\nPlaneOffsetZ:r={:f}\r\nPlaneSize:r={:f}\r\n"
                    "numTexturesWidth:i={}\r\nnumTexturesHeight:i={}\r\n\r\n",
    planeOffsetX, planeOffsetZ, planeSize, numTexturesWidth, numTexturesHeight);


  // Export nodes.

  unsigned int numExportedNodes = 0;
  for (unsigned int nodeNo = 0; nodeNo < cb.node.Count(); nodeNo++)
  {
    if (!cb.node[nodeNo]->GetParentNode() || !cb.node[nodeNo]->GetParentNode()->IsRootNode())
      continue;

    std::string name = wideToStr(cb.node[nodeNo]->GetName());
    if (istarts_with(name, "GroundPlane"))
      continue;

    while (name.length() > 0 && isdigit((unsigned char)name[name.length() - 1]))
      name.erase(name.length() - 1, 1);

    if (name.length() > 0 && name[name.length() - 1] == '_')
      name.erase(name.length() - 1, 1);

    float masterScale = static_cast<float>(GetSystemUnitScale(UNITS_METERS));
    Matrix3 tm = cb.node[nodeNo]->GetNodeTM(0);
    tm.SetTrans(tm.GetTrans() * masterScale + Point3(offsetX, offsetZ, 0.f));
    const Matrix3 m = tm;

    os << std::format("object{{\r\n"
                      "  name:t=\"{}\"\r\n"
                      "  numericName:t=\"{} {}\"\r\n"
                      "  matrix:m=[[{:f}, {:f}, {:f}] [{:f}, {:f}, {:f}] [{:f}, {:f}, {:f}] [{:f}, {:f}, {:f}]]\r\n"
                      "}}\r\n",
      name, name, planeNo * 100000 + numExportedNodes, m[0][0], m[0][2], m[0][1], m[2][0], m[2][2], m[2][1], m[1][0], m[1][2], m[1][1],
      m[3][0], m[3][2], m[3][1]);

    numExportedNodes++;
  }

  os.close();
  if (!os)
  {
    errorMessage(GetString(IDS_FILE_WRITE_ERR));
    return;
  }

  explog(_T("Success!\r\n"));
}


bool ExportENCB::checkDegenerateTriangle(INode *node, Matrix3 &applied_transform, unsigned int index1, unsigned int index2,
  unsigned int index3, Point3 &vertex1, Point3 &vertex2, Point3 &vertex3)
{
  Point3 *degenerateAt = NULL;

  if (index1 == index2)
    degenerateAt = &vertex1;

  if (index1 == index3)
    degenerateAt = &vertex1;

  if (index2 == index3)
    degenerateAt = &vertex2;

  if (is_equal_point(vertex1, vertex2, DEGENERATE_VERTEX_DELTA))
    degenerateAt = &vertex1;

  if (is_equal_point(vertex1, vertex3, DEGENERATE_VERTEX_DELTA))
    degenerateAt = &vertex1;

  if (is_equal_point(vertex2, vertex3, DEGENERATE_VERTEX_DELTA))
    degenerateAt = &vertex2;

  Point3 diff = vertex3 - vertex1;
  Point3 dir = vertex2 - vertex1;
  float sqrlen = dir.LengthSquared();
  float t = DotProd(diff, dir) / sqrlen;
  diff -= t * dir;
  float linedist = diff.Length();

  Point3 center;

  if (linedist < DEGENERATE_VERTEX_DELTA)
  {
    center = (vertex1 + vertex2 + vertex3) / 3;
    degenerateAt = &center;
  }

  if (degenerateAt)
  {
    Point3 transformedPoint = (*degenerateAt) / static_cast<float>(GetSystemUnitScale(UNITS_METERS));
    transformedPoint = Inverse(applied_transform) * node->GetObjTMAfterWSM(0) * transformedPoint;

    explogWarning(_T( "'%s' has degenerate triangle at (%g, %g, %g) (in units)\r\n"), node->GetName(), transformedPoint.x,
      transformedPoint.y, transformedPoint.z);
  }

  return degenerateAt != NULL;
}

void update_export_mode(bool use_legacy_import)
{
  util.exportMode = use_legacy_import ? ExpUtil::ExportMode::Standard : ExpUtil::ExportMode::LayersAsDags;
  util.update_ui_dag(util.hExpDag);
}

//==========================================================================//

enum ExpOps
{
  fun_export
};

class IDagorExportUtil : public FPStaticInterface
{
public:
  DECLARE_DESCRIPTOR(IDagorExportUtil)
  BEGIN_FUNCTION_MAP FN_4(fun_export, TYPE_BOOL, export_dag, TYPE_STRING, TYPE_INTERVAL, TYPE_BOOL, TYPE_BOOL) END_FUNCTION_MAP

    // `range` is a stub. It used to set the animation range and nothing reads it any more, but it is a
    // positional parameter, so it is kept to avoid breaking the maxscripts that pass it.
    BOOL export_dag(const TCHAR *fn, Interval /*range*/, bool selectedOnly, bool suppressPrompts)
  {
    const int bkExpflg = util.expflg;
    const bool bkSuppressPrompts = util.suppressPrompts;

    if (selectedOnly)
      util.expflg |= EXP_SEL;
    else
      util.expflg &= ~EXP_SEL;

    util.suppressPrompts = suppressPrompts;

    util.exp_fname = fn;
    util.update_ui();
    BOOL result = util.export_dag();

    util.suppressPrompts = bkSuppressPrompts;
    util.expflg = bkExpflg;
    util.update_ui();

    return result;
  }
};

static IDagorExportUtil dagorexputiliface(Interface_ID(0x18da32ce, 0x739f1b15), _T ("dagorExport"), IDS_DAGOR_EXPORT_IFACE, NULL,
  FP_CORE, fun_export, _T ("export"), -1, TYPE_BOOL, 0, 4, _T ("filename"), -1, TYPE_STRING, _T ("range"), -1, TYPE_INTERVAL,
  // f_keyArgDefault marks an optional keyArg param. The value
  // after that is its default value.
  _T ("selectedOnly"), -1, TYPE_BOOL, f_keyArgDefault, false, _T ("suppressPrompts"), -1, TYPE_BOOL, f_keyArgDefault, false, p_end);
