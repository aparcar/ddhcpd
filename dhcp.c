/**
 * DHCP Structures
 */

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "dhcp.h"
#include "tools.h"

// Free an offered lease after 12 seconds.
uint16_t DHCP_OFFER_TIMEOUT = 12;
uint16_t DHCP_LEASE_TIME    = 3600;

void printf_lease(dhcp_lease *lease) {
  printf("DHCP LEASE [ state %i, xid %u, end %i ]\n",lease->state,lease->xid,lease->lease_end); 
}

int dhcp_new_lease_block(struct dhcp_lease_block** lease_block,struct in_addr *subnet,uint32_t subnet_len) {
  *lease_block = (struct dhcp_lease_block*) malloc(sizeof(struct dhcp_lease_block));
  if ( ! *lease_block ) return 1;
  memcpy(&(*lease_block)->subnet,subnet,sizeof(struct in_addr));
  (*lease_block)->subnet_len = subnet_len;
  (*lease_block)->addresses = (struct dhcp_lease*) calloc(subnet_len,sizeof(struct dhcp_lease));
  if ( ! (*lease_block)->addresses ) {
    free(*lease_block);
    return 1;
  }
  for ( unsigned int index = 0; index < subnet_len; index++ ) {
    memset(&(*lease_block)->addresses[index].chaddr,0,16);
    (*lease_block)->addresses[index].state = FREE;
    (*lease_block)->addresses[index].lease_end = 0;
  }
  return 0;
}

void dhcp_free_lease_block(struct dhcp_lease_block** lease_block) {
  free((*lease_block)->addresses);
  free(*lease_block);
}

int dhcp_discover(int socket, dhcp_packet *discover,struct dhcp_lease_block *lease_block) {
  time_t now = time(NULL);
  dhcp_lease *lease = NULL;
  int lease_index = 0;
  for( unsigned int index = 0; index < lease_block->subnet_len && lease == NULL; index++) {
    if ( lease_block->addresses[index].state == FREE
       || ( lease_block->addresses[index].state == OFFERED
          && lease_block->addresses[index].lease_end < now )) {
      lease = lease_block->addresses + index;
      lease_index = index;
      memcpy(&lease->chaddr,&discover->chaddr,16);
      lease->xid = discover->xid;
      lease->state = OFFERED;
      lease->lease_end = now + DHCP_OFFER_TIMEOUT;
    }
  }

  if ( lease == NULL ) {
    return -1;
  }

  //struct in_addr tmpaddr;

  dhcp_packet* packet = (dhcp_packet*) calloc(sizeof(dhcp_packet),1);
  packet->op    = 2;
  packet->htype = discover->htype;
  packet->hlen  = discover->hlen;
  packet->hops  = discover->hops;
  packet->xid   = discover->xid;
  packet->secs  = 0;
  packet->flags = discover->flags;
  // ciaddr
  addr_add(&lease_block->subnet,&packet->yiaddr,lease_index);
  // siaddr
  memcpy(&packet->giaddr,&discover->giaddr,4);
  memcpy(&packet->chaddr,&discover->chaddr,16);
  // sname
  // file
  packet->options_len = 5;
  packet->options = (dhcp_option*) calloc(sizeof(dhcp_option) , packet->options_len);
  packet->options[0].code = 53;
  packet->options[0].len = 1;
  packet->options[0].payload = (uint8_t*)  malloc(sizeof(uint8_t) * 1 );
  packet->options[0].payload[0] = 2;

  // subnet mask
  packet->options[1].code = 1;
  packet->options[1].len = 4;
  packet->options[1].payload = (uint8_t*)  malloc(sizeof(uint8_t) * 4 );
  // lease_block
  packet->options[1].payload[0] = 255;
  packet->options[1].payload[1] = 255;
  packet->options[1].payload[2] = 255;
  packet->options[1].payload[3] = 0;

  // broadcast address
  packet->options[2].code = 28;
  packet->options[2].len = 4;
  packet->options[2].payload = (uint8_t*)  malloc(sizeof(uint8_t) * 4 );
  packet->options[2].payload[0] = 10;
  packet->options[2].payload[1] = 0;
  packet->options[2].payload[2] = 0;
  packet->options[2].payload[3] = 255;

  // time offset
  packet->options[3].code = 2;
  packet->options[3].len = 4;
  packet->options[3].payload = (uint8_t*)  malloc(sizeof(uint8_t) * 4 );
  packet->options[3].payload[0] = 0;
  packet->options[3].payload[1] = 0;
  packet->options[3].payload[2] = 0;
  packet->options[3].payload[3] = 0;

  // routers
  packet->options[4].code = 3;
  packet->options[4].len = 4;
  packet->options[4].payload = (uint8_t*)  malloc(sizeof(uint8_t) * 4 );
  packet->options[4].payload[0] = 0;
  packet->options[4].payload[1] = 0;
  packet->options[4].payload[2] = 0;
  packet->options[4].payload[3] = 0;

  printf_dhcp(packet);

  send_dhcp_packet(socket, packet);
  free(packet);
  return 0;
}

int dhcp_request( int socket, struct dhcp_packet *request, ddhcp_block* blocks, ddhcp_config *config ){
  DEBUG("dhcp_request( %i, dhcp_packet, blocks, config)\n",socket);
  // search the lease we may have offered
  ddhcp_block *block = blocks;
  dhcp_lease_block *lease_block = NULL;
  dhcp_lease *lease = NULL ;
  int lease_index = 0;
  for ( uint32_t i = 0; i < config->number_of_blocks; i++) {
    if ( block->state == DDHCP_OURS ) {
      dhcp_lease *lease_iter = block->lease_block->addresses;
      for ( unsigned int j = 0 ; j < block->lease_block->subnet_len ; j++ ) {
        if ( lease_iter->state == OFFERED && lease_iter->xid == request->xid ) {
          if ( memcmp(request->chaddr, lease_iter->chaddr,16) == 0 ) {
            lease_block = block->lease_block;
            lease = lease_iter;
            lease_index = j;
            DEBUG("dhcp_request(...): Found requested lease\n");
            break;
          }
        }
        lease_iter++;
      }
      if ( lease ) break;
    }
    block++;
  }

  if ( !lease ) {
    DEBUG("dhcp_request(...): Requested lease not found\n");
    return 1;
  } 

  dhcp_packet* packet = (dhcp_packet*) calloc(sizeof(dhcp_packet),1);
  packet->op    = 2;
  packet->htype = request->htype;
  packet->hlen  = request->hlen;
  packet->hops  = request->hops;
  packet->xid   = request->xid;
  packet->secs  = 0;
  packet->flags = request->flags;
  memcpy(&packet->ciaddr,&request->ciaddr,4);
  addr_add(&lease_block->subnet,&packet->yiaddr,lease_index);
  // siaddr
  memcpy(&packet->giaddr,&request->giaddr,4);
  memcpy(&packet->chaddr,&request->chaddr,16);
  // sname
  // file
  packet->options_len = 5;
  packet->options = (dhcp_option*) calloc(sizeof(dhcp_option) , packet->options_len);
  packet->options[0].code = 53;
  packet->options[0].len = 1;
  packet->options[0].payload = (uint8_t*)  malloc(sizeof(uint8_t) * 1 );
  packet->options[0].payload[0] = DHCPACK;

  // subnet mask
  packet->options[1].code = 1;
  packet->options[1].len = 4;
  packet->options[1].payload = (uint8_t*)  malloc(sizeof(uint8_t) * 4 );
  // lease_block
  packet->options[1].payload[0] = 255;
  packet->options[1].payload[1] = 255;
  packet->options[1].payload[2] = 255;
  packet->options[1].payload[3] = 0;

  // broadcast address
  packet->options[2].code = 28;
  packet->options[2].len = 4;
  packet->options[2].payload = (uint8_t*)  malloc(sizeof(uint8_t) * 4 );
  packet->options[2].payload[0] = 10;
  packet->options[2].payload[1] = 0;
  packet->options[2].payload[2] = 0;
  packet->options[2].payload[3] = 255;

  // time offset
  packet->options[3].code = 2;
  packet->options[3].len = 4;
  packet->options[3].payload = (uint8_t*)  malloc(sizeof(uint8_t) * 4 );
  packet->options[3].payload[0] = 0;
  packet->options[3].payload[1] = 0;
  packet->options[3].payload[2] = 0;
  packet->options[3].payload[3] = 0;

  // routers
  packet->options[4].code = 3;
  packet->options[4].len = 4;
  packet->options[4].payload = (uint8_t*)  malloc(sizeof(uint8_t) * 4 );
  packet->options[4].payload[0] = 0;
  packet->options[4].payload[1] = 0;
  packet->options[4].payload[2] = 0;
  packet->options[4].payload[3] = 0;

  printf_dhcp(packet);

  send_dhcp_packet(socket, packet);
  free(packet);

  return 0;
}

int dhcp_has_free(struct dhcp_lease_block *lease_block) {
  dhcp_lease *lease = lease_block->addresses;
  for ( unsigned int i = 0 ; i < lease_block->subnet_len ; i++ ) {
    if ( lease->state == FREE ) {
      return 1;
    }
    lease++;
  }
  return 0;
}

int dhcp_num_free( struct dhcp_lease_block *lease_block ) {
  int num = 0;
  dhcp_lease *lease = lease_block->addresses;
  for ( unsigned int i = 0 ; i < lease_block->subnet_len ; i++ ) {
    if ( lease->state == FREE ) {
      num++;
    }
    lease++;
  }
  return num;
}

void dhcp_check_timeouts( dhcp_lease_block * lease_block ) {
  dhcp_lease *lease = lease_block->addresses;
  time_t now = time(NULL);
  for ( unsigned int i = 0 ; i < lease_block->subnet_len ; i++ ) {
    if ( lease->state != FREE && lease->lease_end < now ) {
      printf("Free Lease\n");
      lease->state = FREE;
    }
    lease++;
  }
}
