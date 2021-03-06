#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
#include <iostream>
#include <fstream>

#include "mpxp_help.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "libvo2/img_format.h"
#include "libvo2/sub.h"
#include "xmpcore/xmp_image.h"

#include "libmpconf/cfgparser.h"
#include "libmpconf/m_struct.h"
#include "libmpconf/m_option.h"
#include "menu.h"

#include "libvo2/video_out.h"
#include "osdep/keycodes.h"
#include "pp_msg.h"
#include "mplayerxp.h" // mpxp_context().video().output

struct menu_priv_s {
  char** lines;
  int num_lines;
  int cur_line;
  int disp_lines;
  int minb;
  int hspace;
  char* file;
};

static struct menu_priv_s cfg_dflt = {
  NULL,
  0,
  0,
  0,
  0,
  3,
  NULL
};

#define ST_OFF(m) M_ST_OFF(struct menu_priv_s,m)

static m_option_t cfg_fields[] = {
  { "minbor", ST_OFF(minb), MCONF_TYPE_INT, M_OPT_MIN, 0, 0, NULL },
  { "hspace", ST_OFF(hspace), MCONF_TYPE_INT, M_OPT_MIN, 0, 0, NULL },
  { "file", ST_OFF(file), MCONF_TYPE_STRING, 0, 0, 0, NULL },
  { NULL, NULL, NULL, 0,0,0,NULL }
};

#define mpriv (menu->priv)

static void read_cmd(menu_t* menu,int cmd) {
  switch(cmd) {
  case MENU_CMD_UP:
    mpriv->cur_line -= mpriv->disp_lines / 2;
    if(mpriv->cur_line < 0)
      mpriv->cur_line = 0;
    break;
  case MENU_CMD_DOWN:
  case MENU_CMD_OK:
    mpriv->cur_line += mpriv->disp_lines / 2;
    if(mpriv->cur_line >= mpriv->num_lines)
      mpriv->cur_line = mpriv->num_lines - 1;
    break;
  case MENU_CMD_LEFT:
  case MENU_CMD_CANCEL:
    menu->show = 0;
    menu->cl = 1;
    break;
  }
}

static void read_key(menu_t* menu,int c) {
  switch (c) {
  case KEY_HOME:
    mpriv->cur_line = 0;
    break;
  case KEY_END:
    mpriv->cur_line = mpriv->num_lines - 1;
    break;
  case KEY_PAGE_UP:
    mpriv->cur_line =  mpriv->cur_line > mpriv->disp_lines ?
      mpriv->cur_line - mpriv->disp_lines : 0;
    break;
  case KEY_PAGE_DOWN:
    mpriv->cur_line = mpriv->cur_line + mpriv->disp_lines > mpriv->num_lines - 1 ? mpriv->num_lines - 1 : mpriv->cur_line + mpriv->disp_lines;
    break;
  default:
    menu_dflt_read_key(menu,c);
  }
}


static void draw(menu_t* menu,const mp_image_t& mpi) {
  int x = mpriv->minb;
  int y = mpriv->minb;
  //int th = 2*mpriv->hspace + mpxp_context().video().output->font->height;
  int i,end;

  if(x < 0) x = 8;
  if(y < 0) y = 8;

  mpriv->disp_lines = (mpi.h + mpriv->hspace  - 2*mpriv->minb) / (  mpxp_context().video().output->font->height + mpriv->hspace);
  if(mpriv->num_lines - mpriv->cur_line < mpriv->disp_lines) {
    i = mpriv->num_lines - 1 - mpriv->disp_lines;
    if(i < 0) i = 0;
    end = mpriv->num_lines - 1;
  } else {
    i = mpriv->cur_line;
    end = i + mpriv->disp_lines;
    if(end >= mpriv->num_lines) end = mpriv->num_lines - 1;
  }

  for( ; i < end ; i++) {
    menu_draw_text(mpi,mpriv->lines[i],x,y);
    y += mpxp_context().video().output->font->height + mpriv->hspace;
  }

}

#define BUF_SIZE 1024

static int open_txt(menu_t* menu,const char* args) {
  std::ifstream fd;
  char buf[BUF_SIZE];
  char *l;
  int s;
  int pos = 0, r = 0;
  args = NULL; // Warning kill

  menu->draw = draw;
  menu->read_cmd = read_cmd;
  menu->read_key = read_key;

  if(!mpriv->file) {
    mpxp_warn<<"[libmenu] MenuTxt need a TxtFileName"<<std::endl;
    return 0;
  }

  fd.open(mpriv->file,std::ios_base::in);
  if(!fd.is_open()) {
    mpxp_warn<<"[libmenu] MenuTxt can't open: "<<mpriv->file<<std::endl;
    return 0;
  }

  while(1) {
    fd.read(buf+pos,BUF_SIZE-pos-1);
    r=BUF_SIZE-pos-1;
    if(!fd.good()) {
      if(pos > 0) {
	mpriv->lines = (char **)mp_realloc(mpriv->lines,(mpriv->num_lines + 1)*sizeof(char*));
	mpriv->lines[mpriv->num_lines] = mp_strdup(buf);
	mpriv->num_lines++;
      }
      fd.close();
      break;
    }
    pos += r;
    buf[pos] = '\0';

    while((l = strchr(buf,'\n')) != NULL) {
      s = l-buf;
      mpriv->lines = (char **)mp_realloc(mpriv->lines,(mpriv->num_lines + 1)*sizeof(char*));
      mpriv->lines[mpriv->num_lines] = new char [s+1];
      memcpy(mpriv->lines[mpriv->num_lines],buf,s);
      mpriv->lines[mpriv->num_lines][s] = '\0';
      pos -= s + 1;
      if(pos > 0)
	memmove(buf,l+1,pos);
      buf[pos] = '\0';
      mpriv->num_lines++;
    }
    if(pos >= BUF_SIZE-1) {
      mpxp_warn<<"[libmenu] Warning too long line splitting"<<std::endl;
      mpriv->lines = (char **)mp_realloc(mpriv->lines,(mpriv->num_lines + 1)*sizeof(char*));
      mpriv->lines[mpriv->num_lines] = mp_strdup(buf);
      mpriv->num_lines++;
      pos = 0;
    }
  }

  mpxp_info<<"[libmenu] Parsed lines: "<<mpriv->num_lines<<std::endl;

  return 1;
}

static const m_struct_t m_priv =
{
    "txt_cfg",
    sizeof(struct menu_priv_s),
    &cfg_dflt,
    cfg_fields
};

extern const menu_info_t menu_info_txt = {
  "Text file viewer",
  "txt",
  "Albeu",
  "",
  &m_priv,
  open_txt,
};
