
#include <arpa/inet.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <openssl/sha.h>
#include <unistd.h>
#include <zlib.h>
#define IPV4 "127.0.0.1"
//#define IPV4 "147.32.217.203"
#define PORT 50505
#define BUFFER_S 1024
#define DATA_IN_FRAME 1016
#define NUM_IDX 4
#define DAT_IDX 8

using namespace std;

// functions declaration
void fill_buffer(int frame_num, int data_num, char *buffer_tx, FILE *fd);
int wait_for_ack(int sd, char *buffer_rx, struct sockaddr_in *server,
                 socklen_t server_len);
int *compute_crc(char *buffer, unsigned long crc, int buffer_len);

int main(int argc, char **argv) {
  // constants declaration
  int sd, ret, file_length, frames_num, current_frame = 0;
  // struct hostent *hp;

  struct sockaddr_in server;
  socklen_t server_len;
  FILE *fd;
  char c;
  char buffer_tx[BUFFER_S];
  char buffer_rx[BUFFER_S];
  char num_packet[4];
  char crc_val[4];
  char name_msg[] = "NAME=";
  char size_msg[] = "SIZE=";
  char hash_msg[] = "0000HASH=";
  int buff = 0;
  clock_t c1, c2;
  int time_out;

  fd = fopen(argv[1], "r");
  // set socket adress
  printf("Connect ...\n");
  sd = socket(AF_INET, SOCK_DGRAM, 0);
  // set nonblocking
  unsigned long int noBlock = 1;
  ioctl(sd, FIONBIO, &noBlock);
  server.sin_family = AF_INET;
  server.sin_port = htons(PORT);
  inet_aton(IPV4, &(server.sin_addr));
  server_len = sizeof(server);

  // send name of the file
  strcpy(buffer_tx, name_msg);
  strcat(buffer_tx, argv[1]);
  sendto(sd, &buffer_tx, sizeof(buffer_tx), MSG_DONTWAIT,
         (struct sockaddr *)&server, sizeof(server));

  printf("sending file with name %s\n", buffer_tx);

  // find how big the file is
  fseek(fd, 0, SEEK_END);
  file_length = ftell(fd);
  rewind(fd);
  memset(buffer_tx, 0, BUFFER_S);
  strcpy(buffer_tx, size_msg);
  strcat(buffer_tx, to_string(file_length).c_str());
  sendto(sd, &buffer_tx, sizeof(buffer_tx), MSG_DONTWAIT,
         (struct sockaddr *)&server, sizeof(server));

////////////////////////////////////////////////////// HASH
   unsigned char *file_hash;
   char hash_buff[20];
   file_hash = ( unsigned char*) malloc(file_length);
   for(int i = 0; i < file_length ;i++){
    file_hash[i]=getc(fd);
  }

  rewind(fd); 

  SHA1(file_hash	, file_length,(unsigned char*)hash_buff);
  for(int i = 0; i < 20 ;i++){
    cout << (int)hash_buff[i] << ":";


  }

  free(file_hash);


  
  memset(buffer_tx, 0, BUFFER_S);
  strcpy(buffer_tx, hash_msg);

  for(int i = 0; i < 20 ;i++){
    buffer_tx[i+9]=hash_buff[i];
  }



  sendto(sd, &buffer_tx, 29, MSG_DONTWAIT,
         (struct sockaddr *)&server, sizeof(server));
////////////////////////////////////////////////////////////////////





  // some constants needed further
  frames_num = (file_length + DATA_IN_FRAME - 1) / DATA_IN_FRAME;
  cout << "file length is: " << file_length << endl;
  cout << "number of frames is: " << frames_num << endl;
  sprintf(num_packet, "%d", current_frame);
  int nos = 0;
  int to_fill;
  int ack_rcv;
  size_t send_size;
  // sending data
  while (nos != file_length) {
    // kolik jeste bajtu poslat
    if (file_length - nos >= DATA_IN_FRAME) {
      to_fill = DATA_IN_FRAME;
    } else {
      to_fill = file_length - nos;
    }
    send_size = (size_t)(to_fill+8);
    printf("send size je %d\n", (int)send_size);
    fill_buffer(current_frame, to_fill, buffer_tx, fd);
    ret = sendto(sd, &buffer_tx, send_size, MSG_DONTWAIT,
           (struct sockaddr *)&server, sizeof(server));
    cout << ret << endl;
    nos += to_fill;
    // wait for ack, if not recieved resend
    ack_rcv = wait_for_ack(sd, buffer_rx, &server, server_len);
    cout << ack_rcv << endl;
    while (ack_rcv != 0) {
      ret = sendto(sd, &buffer_tx, send_size, MSG_DONTWAIT,
             (struct sockaddr *)&server, sizeof(server));
      cout << ret << endl;
      ack_rcv = wait_for_ack(sd, buffer_rx, &server, server_len);
    }
    current_frame++;
  }
  close(sd);

  return 0;
}

void fill_buffer(int frame_num, int data_num, char *buffer_tx, FILE *fd) {
  char data_chr[4];
  char data_buffer[1016];
  char c;
  if (data_num != DATA_IN_FRAME) {
    memset(buffer_tx, 0, BUFFER_S);
  }
  sprintf(data_chr, "%d", frame_num);
  for (int i = NUM_IDX; i < NUM_IDX + 4; i++) {
    buffer_tx[i] = data_chr[i - NUM_IDX];
  }
  // todo CRC , vylepsit nahravani

  // menim i i < DAT_IDX + data_num za BUFFER_S, protoze v clientovi pak delam
  // CRCcko jenom ze zbyvajicich bajtu packetu, kdyzto server bere furt 1024
  // tahle uprava je asi hovno, chce to spis dodelat aby server vedel ze ten
  // posledni packet neni celej to co posilam uz z clienta ma podle me vzdycky
  // 1024, jenom crcko je pocitane ze zbytku dat

  for (int i = DAT_IDX; i < DAT_IDX+data_num; i++) {
    buffer_tx[i] = getc(fd);
    data_buffer[i - 8] = buffer_tx[i];
  }

  unsigned long crc = crc32(0L, Z_NULL, 0);
   
  cout<<"odesilam tolik dat"<< DAT_IDX+data_num;
  int *crc_cal = compute_crc(data_buffer, crc, DAT_IDX+data_num-8);

  for (int i = 0; i < 4; i++) {
    buffer_tx[i] = crc_cal[i];
    cout << crc_cal[i];
    cout << ":";
  }
}

int wait_for_ack(int sd, char *buffer_rx, struct sockaddr_in *server,
                 socklen_t server_len) {
  char ack_msg[] = "ACK";
  char resend_msg[] = "RESEND";
  int ret;
  int time_out = 0;
  ret = recvfrom(sd, buffer_rx, sizeof(buffer_rx), 0, (struct sockaddr *)server,
                 &server_len);
  while (ret < 0) {
    usleep(10000);
    time_out += 10;
    if (time_out >= 1000) {
      return 1;
    }
    ret = recvfrom(sd, buffer_rx, sizeof(buffer_rx), 0,
                   (struct sockaddr *)server, &server_len);
  }

  // kdyz misto ACK prijde noACK tak poslat packet znova- TODO

  cout << buffer_rx << " " << strlen(buffer_rx) << endl;
  if (strcmp(ack_msg, buffer_rx) == 0) {
    return 0;
  }
  else if (strcmp(resend_msg, buffer_rx) == 0){
            cout << "RESEND requested" << endl;
          return -1;
          }
  else {
    cout << "Message corrupted not ACK nor RESEND" << endl;
    return -100;
  }
}
// return 0;
//}

int *compute_crc(char *buffer, unsigned long crc, int buffer_len) {

  unsigned char data[buffer_len];
  memset(data, 0, buffer_len);

  for (int i = 0; i < buffer_len; i++) {
    data[i] = buffer[i];
  }

  crc = crc32(crc, (const unsigned char *)data, buffer_len);
  string r;
  for (int i = 0; i < 32; i++) {
    r = (crc % 2 == 0 ? "0" : "1") + r;
    crc /= 2;
  }

  static int crc_4[4];
  int i = 0;
  for (i = 0; i < 4; i++) {
    // v rku je binarni string crcka (32 bitu), prevadim to na 4 8 bitove
    // integery, -48 protoze 1 je ascii 49
    crc_4[i] = 128 * ((int)r[i * 8] - 48) + 64 * ((int)r[i * 8 + 1] - 48) +
               32 * ((int)r[i * 8 + 2] - 48) + 16 * ((int)r[i * 8 + 3] - 48) +
               8 * ((int)r[i * 8 + 4] - 48) + 4 * ((int)r[i * 8 + 5] - 48) +
               2 * ((int)r[i * 8 + 6] - 48) + 1 * ((int)r[i * 8 + 7] - 48);
  }
  return crc_4;
}
