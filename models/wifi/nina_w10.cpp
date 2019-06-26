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
#include <vector>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>




class Nina;

class Nina_qspi_itf : public Qspi_itf
{
public:
  Nina_qspi_itf(Nina *top) : top(top) {}
  void sck_edge(int64_t timestamp, int sck, int data_0, int data_1, int data_2, int data_3, int mask);
  void edge(int64_t timestamp, int data_0, int data_1, int data_2, int data_3, int mask);
  void cs_edge(int64_t timestamp, int cs);

private:
    Nina *top;
};



class Nina : public Dpi_model
{
  friend class Nina_qspi_itf;

public:
  Nina(js::config *config, void *handle);

protected:

  void sck_edge(int64_t timestamp, int sck, int sdio0, int sdio1, int sdio2, int sdio3, int mask);
  void edge(int64_t timestamp, int sdio0, int sdio1, int sdio2, int sdio3, int mask);
  void cs_edge(int64_t timestamp, int cs);
  void handle_clk_high(int64_t timestamp, int sdio0, int sdio1, int sdio2, int sdio3, int mask);
  void handle_clk_low(int64_t timestamp, int sdio0, int sdio1, int sdio2, int sdio3, int mask);


private:

  Nina_qspi_itf *qspi0;
  int current_cs;
  void *trace;
  int m_socket;
  bool active;
  int pending_bits;
  uint8_t pending_word;
};



Nina::Nina(js::config *config, void *handle) : Dpi_model(config, handle)
{
  struct sockaddr_in addr;
  struct hostent *he;

  qspi0 = new Nina_qspi_itf(this);
  create_itf("input", static_cast<Dpi_itf *>(qspi0));

  this->pending_bits = 0;

  this->trace = this->trace_new("wifi");

  print("Creating NINA W10 model");


  int m_port = config->get("port")->get_int();
  char *m_server = (char *)config->get_child_str("addr").c_str();

  if((this->m_socket = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    fprintf(stderr, "Unable to create socket (%s)\n", strerror(errno));
    throw;
  }

  if((he = gethostbyname(m_server)) == NULL) {
    perror("gethostbyname");
    throw;
  }

  addr.sin_family = AF_INET;
  addr.sin_port = htons(m_port);
  addr.sin_addr = *((struct in_addr *)he->h_addr_list[0]);
  memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));

  if(::connect(m_socket, (struct sockaddr *)&addr, sizeof(addr)) == -1)
  {
    this->print("Unable to connect to %s port %d (%s)\n", m_server, m_port,
            strerror(errno));
    this->m_socket = -1;
  }

}




void Nina_qspi_itf::cs_edge(int64_t timestamp, int cs)
{
  top->cs_edge(timestamp, cs);
}

void Nina_qspi_itf::sck_edge(int64_t timestamp, int sck, int data_0, int data_1, int data_2, int data_3, int mask)
{
  top->sck_edge(timestamp, sck, data_0, data_1, data_2, data_3, mask);
}

void Nina_qspi_itf::edge(int64_t timestamp, int data_0, int data_1, int data_2, int data_3, int mask)
{
  top->edge(timestamp, data_0, data_1, data_2, data_3, mask);
}

void Nina::cs_edge(int64_t timestamp, int cs)
{
  this->active = !cs;

  this->trace_msg(this->trace, 4, "CS edge (timestamp: %ld, cs: %d)", timestamp, cs);
}

void Nina::edge(int64_t timestamp, int sdio0, int sdio1, int sdio2, int sdio3, int mask)
{
  this->trace_msg(this->trace, 4, "Edge (timestamp: %ld, data_0: %d, data_1: %d, data_2: %d, data_3: %d, mask: 0x%x)", timestamp, sdio0, sdio1, sdio2, sdio3, mask);

  static int count = 0;

  if (this->active)
  {
    this->pending_word = (this->pending_word << 1) | sdio0;
    this->pending_bits++;

    if (this->pending_bits == 8)
    {
      this->pending_bits = 0;

      ::send(this->m_socket, (void *)&this->pending_word, 1, 0);
    }
  }
}

void Nina::handle_clk_high(int64_t timestamp, int sdio0, int sdio1, int sdio2, int sdio3, int mask)
{
  this->trace_msg(this->trace, 4, "HANDLE CLK HIGH");
}

void Nina::handle_clk_low(int64_t timestamp, int sdio0, int sdio1, int sdio2, int sdio3, int mask)
{
  this->trace_msg(this->trace, 4, "HANDLE CLK LOW");
}

void Nina::sck_edge(int64_t timestamp, int sck, int sdio0, int sdio1, int sdio2, int sdio3, int mask)
{
  this->trace_msg(this->trace, 4, "SCK edge (timestamp: %ld, sck: %d, data_0: %d, data_1: %d, data_2: %d, data_3: %d, mask: 0x%x)", timestamp, sck, sdio0, sdio1, sdio2, sdio3, mask);
}

extern "C" Dpi_model *dpi_model_new(js::config *config, void *handle)
{
  return new Nina(config, handle);
}
