//
// "$Id$"
//
// Definition of Apple Cocoa window driver.
//
// Copyright 1998-2016 by Bill Spitzak and others.
//
// This library is free software. Distribution and use rights are outlined in
// the file "COPYING" which should have been included with this file.  If this
// file is missing or damaged, see the license at:
//
//     http://www.fltk.org/COPYING.php
//
// Please report all bugs and problems on the following page:
//
//     http://www.fltk.org/str.php
//


#include "../../config_lib.h"
#include <FL/Fl.H>
#include <FL/Fl_Image.H>
#include <FL/Fl_Bitmap.H>
#include <FL/Fl_Window.H>
#include <FL/x.H>
#include "Fl_WinAPI_Window_Driver.H"
#include <windows.h>

Fl_Window_Driver *Fl_Window_Driver::newWindowDriver(Fl_Window *w)
{
  return new Fl_WinAPI_Window_Driver(w);
}


Fl_WinAPI_Window_Driver::Fl_WinAPI_Window_Driver(Fl_Window *win)
: Fl_Window_Driver(win)
{
  icon_ = new Fl_Window_Driver::icon_data;
  memset(icon_, 0, sizeof(Fl_Window_Driver::icon_data));
}

Fl_WinAPI_Window_Driver::~Fl_WinAPI_Window_Driver()
{
  if (shape_data_) {
    delete shape_data_->todelete_;
    delete shape_data_;
  }
}

void Fl_WinAPI_Window_Driver::shape_bitmap_(Fl_Image* b) {
  shape_data_->shape_ = b;
}

void Fl_WinAPI_Window_Driver::shape_alpha_(Fl_Image* img, int offset) {
  int i, j, d = img->d(), w = img->w(), h = img->h(), bytesperrow = (w+7)/8;
  unsigned u;
  uchar byte, onebit;
  // build an Fl_Bitmap covering the non-fully transparent/black part of the image
  const uchar* bits = new uchar[h*bytesperrow]; // to store the bitmap
  const uchar* alpha = (const uchar*)*img->data() + offset; // points to alpha value of rgba pixels
  for (i = 0; i < h; i++) {
    uchar *p = (uchar*)bits + i * bytesperrow;
    byte = 0;
    onebit = 1;
    for (j = 0; j < w; j++) {
      if (d == 3) {
        u = *alpha;
        u += *(alpha+1);
        u += *(alpha+2);
      }
      else u = *alpha;
      if (u > 0) { // if the pixel is not fully transparent/black
        byte |= onebit; // turn on the corresponding bit of the bitmap
      }
      onebit = onebit << 1; // move the single set bit one position to the left
      if (onebit == 0 || j == w-1) {
        onebit = 1;
        *p++ = byte; // store in bitmap one pack of bits
        byte = 0;
      }
      alpha += d; // point to alpha value of next pixel
    }
  }
  Fl_Bitmap* bitmap = new Fl_Bitmap(bits, w, h);
  bitmap->alloc_array = 1;
  shape_bitmap_(bitmap);
  shape_data_->todelete_ = bitmap;
}

void Fl_WinAPI_Window_Driver::shape(const Fl_Image* img) {
  if (shape_data_) {
    if (shape_data_->todelete_) { delete shape_data_->todelete_; }
  }
  else {
    shape_data_ = new shape_data_type;
  }
  memset(shape_data_, 0, sizeof(shape_data_type));
  pWindow->border(false);
  int d = img->d();
  if (d && img->count() >= 2) shape_pixmap_((Fl_Image*)img);
  else if (d == 0) shape_bitmap_((Fl_Image*)img);
  else if (d == 2 || d == 4) shape_alpha_((Fl_Image*)img, d - 1);
  else if ((d == 1 || d == 3) && img->count() == 1) shape_alpha_((Fl_Image*)img, 0);
}


static inline BYTE bit(int x) { return (BYTE)(1 << (x%8)); }

static HRGN bitmap2region(Fl_Image* image) {
  HRGN hRgn = 0;
  /* Does this need to be dynamically determined, perhaps? */
  const int ALLOC_UNIT = 100;
  DWORD maxRects = ALLOC_UNIT;
  
  RGNDATA* pData = (RGNDATA*)malloc(sizeof(RGNDATAHEADER)+(sizeof(RECT)*maxRects));
  pData->rdh.dwSize = sizeof(RGNDATAHEADER);
  pData->rdh.iType = RDH_RECTANGLES;
  pData->rdh.nCount = pData->rdh.nRgnSize = 0;
  SetRect(&pData->rdh.rcBound, MAXLONG, MAXLONG, 0, 0);
  
  const int bytesPerLine = (image->w() + 7)/8;
  BYTE* p, *data = (BYTE*)*image->data();
  for (int y = 0; y < image->h(); y++) {
    // each row, left to right
    for (int x = 0; x < image->w(); x++) {
      int x0 = x;
      while (x < image->w()) {
        p = data + x / 8;
        if (!((*p) & bit(x))) break; // transparent pixel
        x++;
      }
      if (x > x0) {
        RECT *pr;
        /* Add the pixels (x0, y) to (x, y+1) as a new rectangle
         * in the region
         */
        if (pData->rdh.nCount >= maxRects) {
          maxRects += ALLOC_UNIT;
          pData = (RGNDATA*)realloc(pData, sizeof(RGNDATAHEADER)
                                    + (sizeof(RECT)*maxRects));
        }
        pr = (RECT*)&pData->Buffer;
        SetRect(&pr[pData->rdh.nCount], x0, y, x, y+1);
        if (x0 < pData->rdh.rcBound.left)
          pData->rdh.rcBound.left = x0;
        if (y < pData->rdh.rcBound.top)
          pData->rdh.rcBound.top = y;
        if (x > pData->rdh.rcBound.right)
          pData->rdh.rcBound.right = x;
        if (y+1 > pData->rdh.rcBound.bottom)
          pData->rdh.rcBound.bottom = y+1;
        pData->rdh.nCount++;
        /* On Windows98, ExtCreateRegion() may fail if the
         * number of rectangles is too large (ie: >
         * 4000). Therefore, we have to create the region by
         * multiple steps.
         */
        if (pData->rdh.nCount == 2000) {
          HRGN h = ExtCreateRegion(NULL, sizeof(RGNDATAHEADER)
                                   + (sizeof(RECT)*maxRects), pData);
          if (hRgn) {
            CombineRgn(hRgn, hRgn, h, RGN_OR);
            DeleteObject(h);
          } else
            hRgn = h;
          pData->rdh.nCount = 0;
          SetRect(&pData->rdh.rcBound, MAXLONG, MAXLONG, 0, 0);
        }
      }
    }
    /* Go to next row */
    data += bytesPerLine;
  }
  /* Create or extend the region with the remaining rectangles*/
  HRGN h = ExtCreateRegion(NULL, sizeof(RGNDATAHEADER)
                           + (sizeof(RECT)*maxRects), pData);
  if (hRgn) {
    CombineRgn(hRgn, hRgn, h, RGN_OR);
    DeleteObject(h);
  } else hRgn = h;
  free(pData); // I've created the region so I can free this now, right?
  return hRgn;
}


void Fl_WinAPI_Window_Driver::draw() {
  if (shape_data_) {
    if ((shape_data_->lw_ != pWindow->w() || shape_data_->lh_ != pWindow->h()) && shape_data_->shape_) {
      // size of window has changed since last time
      shape_data_->lw_ = pWindow->w();
      shape_data_->lh_ = pWindow->h();
      Fl_Image* temp = shape_data_->shape_->copy(shape_data_->lw_, shape_data_->lh_);
      HRGN region = bitmap2region(temp);
      SetWindowRgn(fl_xid(pWindow), region, TRUE); // the system deletes the region when it's no longer needed
      delete temp;
    }
  }  Fl_Window_Driver::draw();
}

void Fl_WinAPI_Window_Driver::icons(const Fl_RGB_Image *icons[], int count) {
  free_icons();
  
  if (count > 0) {
    icon_->icons = new Fl_RGB_Image*[count];
    icon_->count = count;
    // FIXME: Fl_RGB_Image lacks const modifiers on methods
    for (int i = 0;i < count;i++)
      icon_->icons[i] = (Fl_RGB_Image*)((Fl_RGB_Image*)icons[i])->copy();
  }
  
  if (Fl_X::i(pWindow))
    Fl_X::i(pWindow)->set_icons();
}

const void *Fl_WinAPI_Window_Driver::icon() const {
  return icon_->legacy_icon;
}

void Fl_WinAPI_Window_Driver::icon(const void * ic) {
  free_icons();
  icon_->legacy_icon = ic;
}

void Fl_WinAPI_Window_Driver::free_icons() {
  int i;
  icon_->legacy_icon = 0L;
  if (icon_->icons) {
    for (i = 0;i < icon_->count;i++)
      delete icon_->icons[i];
    delete [] icon_->icons;
    icon_->icons = 0L;
  }
  icon_->count = 0;
  if (icon_->big_icon)
    DestroyIcon(icon_->big_icon);
  if (icon_->small_icon)
    DestroyIcon(icon_->small_icon);
  icon_->big_icon = NULL;
  icon_->small_icon = NULL;
}

void Fl_WinAPI_Window_Driver::icons(HICON big_icon, HICON small_icon)
{
  free_icons();
  
  if (big_icon != NULL)
    icon_->big_icon = CopyIcon(big_icon);
  if (small_icon != NULL)
    icon_->small_icon = CopyIcon(small_icon);
  
  if (Fl_X::i(pWindow))
    Fl_X::i(pWindow)->set_icons();
}

void Fl_WinAPI_Window_Driver::wait_for_expose() {
  if (!pWindow->shown()) return;
  Fl_X *i = Fl_X::i(pWindow);
  while (!i || i->wait_for_expose) {
    Fl::wait();
  }
}

//
// End of "$Id$".
//