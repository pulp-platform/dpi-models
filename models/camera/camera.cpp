/*
 * Copyright (C) 2018 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 */

#include "dpi/models.hpp"
#include <stdint.h>
#ifdef __MAGICK__
#include <Magick++.h>
#endif

#ifdef __MAGICK__
using namespace Magick;
#endif
using namespace std;

class Camera;


class Camera_stream {

public:
  Camera_stream(Camera *top, string path, int color_mode);
  bool fetch_image();
  unsigned int get_pixel();
  void set_image_size(int width, int height);

private:
  Camera *top;
  string stream_path;
  int frame_index;
#ifdef __MAGICK__
  Image image;
#endif
  int width;
  int height;
#ifdef __MAGICK__
  PixelPacket *image_buffer;
#endif
  int current_pixel;
  int nb_pixel;
  int color_mode;
};


class Camera : public Dpi_model
{
public:
  Camera(js::config *config, void *handle);

  void start();

private:

  void dpi_task();
  static void dpi_task_stub(Camera *);
  void clock_gen();

  Cpi_itf *cpi;

  int64_t period;
  int64_t frequency;

  int pclk;

  int width;
  int height;

  int nb_images;

  int color_mode;
  int pclk_value;
  int state;
  int cnt;
  int targetcnt;
  int lineptr;
  int colptr;
  int bytesel;
  int framesel;

  Camera_stream *stream;
};

enum {
STATE_INIT,
STATE_SOF,
STATE_WAIT_SOF,
STATE_SEND_LINE,
STATE_WAIT_EOF
};

enum {
  COLOR_MODE_GRAY,
  COLOR_MODE_RGB565,
};

#define TP 2
#define TLINE (width+144)*TP


Camera::Camera(js::config *config, void *handle) : Dpi_model(config, handle)
{
#ifdef __MAGICK__
  InitializeMagick(NULL);
#endif

  frequency = 1000000;
  period = 1000000000 / frequency;

  cpi = new Cpi_itf();
  create_itf("cpi", static_cast<Cpi_itf *>(cpi));

  this->stream = NULL;

  // Default color mode is 8bit gray
  this->color_mode = COLOR_MODE_GRAY;
  this->width = 324;
  this->height = 244;

  // Default color mode is 16bits RGB565
  //color_mode = COLOR_MODE_RGB565;
  js::config *stream_config = config->get("image-stream");
  if (stream_config)
  {
    string stream_path = stream_config->get_str();
    this->stream = new Camera_stream(this, stream_path.c_str(), this->color_mode);
    this->stream->set_image_size(this->width, this->height);
  }

}

void Camera::start()
{
  if (this->stream)
    create_periodic_handler(this->period/2, (void *)&Camera::dpi_task_stub, this);

  this->pclk_value = 0;
  this->state = STATE_INIT;

}

void Camera::dpi_task_stub(Camera *_this)
{
  _this->clock_gen();
}

void Camera::clock_gen()
{
  int vsync = 0;
  int href = 0;
  int data = 0;

  this->pclk_value ^= 1;

  if (this->pclk_value)
  {
    switch (this->state) {
      case STATE_INIT:
        this->print("State INIT\n");
        this->cnt = 0;
        this->targetcnt = 3*TLINE;
        this->state = STATE_SOF;
        this->bytesel = 0;
        this->framesel = 0;
        break;

      case STATE_SOF:
        this->print("State SOF (cnt: %d, targetcnt: %d)\n", this->cnt, this->targetcnt);
        vsync = 1;
        this->cnt++;
        if (this->cnt == this->targetcnt) {
          this->cnt = 0;
          this->targetcnt = 17*TLINE;
          this->state = STATE_WAIT_SOF;
        }
        break;

      case STATE_WAIT_SOF:
        this->print("State WAIT_SOF (cnt: %d, targetcnt: %d)\n", this->cnt, this->targetcnt);
        this->cnt++;
        if (this->cnt == this->targetcnt) {
          this->state = STATE_SEND_LINE;
          this->lineptr = 0;
          this->colptr = 0;
        }
        break;

      case STATE_SEND_LINE: {
        href = 1;

        if (this->color_mode == COLOR_MODE_GRAY)
        {
          this->bytesel = 1;
          if (stream)
          {
            data = stream->get_pixel();
          }

          //if (stimImg != NULL) {
          //  pixel = ((uint32_t *)stimImg[framesel])[(lineptr*width)+2*colptr+offset];
          //}

          //data = 0.2989 * ((pixel >> 16) & 0xff) +
          //       0.5870 * ((pixel >>  8) & 0xff) +
          //       0.1140 * ((pixel >>  0) & 0xff);
        }
        else
        {
          int pixel  = 0;

          if (stream)
          {
            pixel = stream->get_pixel();
          }

          //if (stimImg != NULL) {
          //  ((uint32_t *)stimImg[framesel])[(lineptr*width)+colptr];
          //}

          // Coded with RGB565
          if (bytesel) data = (((pixel >> 10) & 0x7) << 5) | (((pixel >> 3) & 0x1f) << 0);
          else         data = (((pixel >> 19) & 0x1f) << 3) | (((pixel >> 13) & 0x7) << 0);
        }

        if (this->bytesel == 1) {
          this->bytesel = 0;
          if(this->colptr == (this->width-1)) {
            this->colptr = 0;
            if(this->lineptr == (this->height-1)) {
              this->state = STATE_WAIT_EOF;
              this->cnt = 0;
              this->targetcnt = 10*TLINE;
              this->lineptr = 0;
            } else {
              this->lineptr = this->lineptr + 1;
            }
          } else {
            this->colptr = this->colptr + 1;
          }

        } else {
          this->bytesel = 1;
        }
        this->print("State SEND_LINE (data: 0x%x)\n", data);
        break;
      }

      case STATE_WAIT_EOF:
        this->print("State WAIT_EOF (cnt: %d, targetcnt: %d)\n", this->cnt, this->targetcnt);
        this->cnt++;
        if (this->cnt == this->targetcnt) {
          this->state = STATE_SOF;
          this->cnt = 0;
          this->targetcnt = 3*TLINE;
          this->framesel++;
          if (this->framesel == this->nb_images) this->framesel = 0;
        }
        break;
    }
  }

  this->cpi->edge(this->pclk_value, href, vsync, data);
}

void Camera::dpi_task()
{
  this->cpi->edge(0, 0, 0, 0);

  while(1) {
    this->wait_ps(period/2);
    this->clock_gen();
  }
}


Camera_stream::Camera_stream(Camera *top, string path, int color_mode)
 : top(top), stream_path(path), frame_index(0), current_pixel(0), nb_pixel(0), color_mode(color_mode)
{
#ifdef __MAGICK__
  image_buffer = NULL;
#endif
}

void Camera_stream::set_image_size(int width, int height)
{
  this->width = width;
  this->height = height;
  nb_pixel = width * height;
}

bool Camera_stream::fetch_image()
{
  char path[strlen(stream_path.c_str()) + 100];
  while(1)
  {
    sprintf(path, stream_path.c_str(), frame_index);

#ifdef __MAGICK__
    try {
      image.read(path);
      break;
    }
    catch( Exception &error_ ) {
      if (frame_index == 0) {
        throw;
      }
    }
#endif

    frame_index = 0;
  }

  //dpi_print(top->handle, ("Opened image (path: " + string(path) + ")\n").c_str());
  frame_index++;

#ifdef __MAGICK__
  image.extent(Geometry(width, height));

  if (color_mode == COLOR_MODE_GRAY)
  {
    image.quantizeColorSpace( GRAYColorspace );
    image.quantizeColors( 256 );
    image.quantize( );
  }


  image_buffer = (PixelPacket*) image.getPixels(0, 0, width, height);
#endif

  return true;
}

unsigned int Camera_stream::get_pixel()
{
#ifdef __MAGICK__
  if (image_buffer == NULL) fetch_image();

  PixelPacket *pixel = &image_buffer[current_pixel];
  current_pixel++;
  if (current_pixel == nb_pixel)
  {
    current_pixel = 0;
    image_buffer = NULL;
  }

  unsigned int shift = 8;
  if (color_mode == COLOR_MODE_GRAY)
  {
    return pixel->red >> shift;
  }
  else
  {
    unsigned char red = pixel->red >> shift;
    unsigned char green = pixel->green >> shift;
    unsigned char blue = pixel->blue >> shift;
    return (red << 16) | (green << 8) | blue;
  }
#else
  return 0;
#endif
}



extern "C" Dpi_model *dpi_model_new(js::config *config, void *handle)
{
  return new Camera(config, handle);
}
