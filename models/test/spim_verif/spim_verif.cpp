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

#define SPIM_VERIF_CMD_BIT        24
#define SPIM_VERIF_CMD_WIDTH      8
#define SPIM_VERIF_CMD_INFO_BIT   0
#define SPIM_VERIF_CMD_INFO_WIDTH 24

#define SPIM_VERIF_FIELD_GET(value,bit,width) (((value) >> (bit)) & ((1<<(width))-1))

typedef enum {
  STATE_GET_CMD,
  STATE_WRITE_CMD,
  STATE_READ_CMD
} Spim_verif_state_e;

typedef enum {
  SPIM_VERIF_CMD_WRITE = 1,
  SPIM_VERIF_CMD_READ = 2
} spim_cmd_e;

class Spim_verif;

class Spim_verif_qspi_itf : public Qspi_itf
{
public:
  Spim_verif_qspi_itf(Spim_verif *top) : top(top) {}
  void sck_edge(int64_t timestamp, int sck, int data_0, int data_1, int data_2, int data_3);

private:
    Spim_verif *top;
};

class Spim_verif : public Dpi_model
{
  friend class Spim_verif_qspi_itf;

public:
  Spim_verif(js::config *config);

protected:

  void sck_edge(int64_t timestamp, int sdio0, int sdio1, int sdio2, int sdio3, int sck);


private:

  Spim_verif_qspi_itf *qspi0;

  void handle_read(uint32_t cmd);
  void handle_write(uint32_t cmd);

  void exec_write(int data);
  void exec_read();

  void handle_command(uint32_t cmd);

  Spim_verif_state_e state = STATE_GET_CMD;
  uint32_t current_cmd = 0;
  int prev_sck = 0;
  int cmd_count = 0;
  int current_addr;
  int current_size;
  unsigned char *data;
  int nb_bits;
  uint32_t byte;
  bool verbose;
};


extern "C" void dummy_func2();


Spim_verif::Spim_verif(js::config *config) : Dpi_model(config)
{
  int mem_size = config->get("mem_size")->get_int();
  verbose = config->get("verbose")->get_bool();
  print("Creating SPIM VERIF model (mem_size: 0x%x)", mem_size);
  data = new unsigned char[mem_size];
  qspi0 = new Spim_verif_qspi_itf(this);
  create_itf("spi", static_cast<Dpi_itf *>(qspi0));
}

void Spim_verif::handle_read(uint32_t cmd)
{
  int size = SPIM_VERIF_FIELD_GET(cmd, SPIM_VERIF_CMD_INFO_BIT, SPIM_VERIF_CMD_INFO_WIDTH);

  if (verbose) print("Handling read command (size: 0x%x)", size);

  state = STATE_READ_CMD;
  current_addr = 0;
  current_size = size;
  nb_bits = 0;
}

void Spim_verif::handle_write(uint32_t cmd)
{
  int size = SPIM_VERIF_FIELD_GET(cmd, SPIM_VERIF_CMD_INFO_BIT, SPIM_VERIF_CMD_INFO_WIDTH);

  if (verbose) print("Handling write command (size: 0x%x)", size);

  state = STATE_WRITE_CMD;
  current_addr = 0;
  current_size = size;
  nb_bits = 0;
}

void Spim_verif::exec_read()
{
  if (nb_bits == 0)
  {
    byte = data[current_addr];
    if (verbose) print("Read byte from memory (value: 0x%x, rem_size: 0x%x)", byte, current_size);
    nb_bits = 8;
    current_addr++;
  }

  int bit = (byte >> 7) & 1;
  byte <<= 1;

  qspi0->set_data(bit);

  nb_bits--;
  current_size--;
  if (current_size == 0)
  {
    state = STATE_GET_CMD;
  }
}

void Spim_verif::exec_write(int val)
{
  data[current_addr] = (data[current_addr] << 1)| val;
  nb_bits++;
  if (nb_bits == 8)
  {
    if (verbose) print("Wrote byte to memory (value: 0x%x, rem_size: 0x%x)", data[current_addr], current_size-1);
    nb_bits = 0;
    current_addr++;
  }
  current_size--;
  if (current_size == 0)
  {
    if (nb_bits != 0)
    {
      if (verbose) print("Wrote byte to memory (value: 0x%x)", data[current_addr]);
    }
    state = STATE_GET_CMD;
  }
}

void Spim_verif::handle_command(uint32_t cmd)
{
  if (verbose) print("Handling command %x", current_cmd);

  int cmd_id = SPIM_VERIF_FIELD_GET(cmd, SPIM_VERIF_CMD_BIT, SPIM_VERIF_CMD_WIDTH);

  switch (cmd_id) {
    case SPIM_VERIF_CMD_WRITE: handle_write(cmd); break;
    case SPIM_VERIF_CMD_READ: handle_read(cmd); break;
    default: print("WARNING: received unknown command: 0x%x", cmd);
  }

}

void Spim_verif_qspi_itf::sck_edge(int64_t timestamp, int sck, int data_0, int data_1, int data_2, int data_3)
{
  top->sck_edge(timestamp, sck, data_0, data_1, data_2, data_3);
}

void Spim_verif::sck_edge(int64_t timestamp, int sck, int sdio0, int sdio1, int sdio2, int sdio3)
{
  if (verbose) print("SCK edge (timestamp: %ld, sck: %d, data_0: %d, data_1: %d, data_2: %d, data_3: %d)", timestamp, sck, sdio0, sdio1, sdio2, sdio3);

  if (prev_sck == 1 && !sck)
  {
    if (state == STATE_READ_CMD)
    {
      exec_read();
    }
  }
  else if (prev_sck == 0 && sck)
  {
    if (state == STATE_GET_CMD)
    {
      current_cmd = (current_cmd << 1) | sdio0;
      cmd_count++;
      if (cmd_count == 32)
      {
        cmd_count = 0;
        handle_command(current_cmd);
      }
    }
    else if (state == STATE_WRITE_CMD)
    {
      exec_write(sdio0);
    }
  }
  prev_sck = sck;
}

extern "C" Dpi_model *dpi_model_new(js::config *config)
{
  return new Spim_verif(config);
}
