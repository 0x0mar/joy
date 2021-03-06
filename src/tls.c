/*
 *	
 * Copyright (c) 2016 Cisco Systems, Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 
 *   Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided
 *   with the distribution.
 * 
 *   Neither the name of the Cisco Systems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * tls.c
 *
 * contains the functionality for TLS awareness
 * 
 */

#include <stdio.h>    /* for fprintf(), etc */
#include <pcap.h>     /* for pcap_hdr       */
#include <ctype.h>    /* for isprint()      */
#include <string.h>   /* for memcpy()       */
#include <stdlib.h>
#include <netinet/in.h>
#include "tls.h"

/* initialize data associated with TLS */
void tls_record_init(struct tls_information *r) {
  r->tls_op = 0;
  r->num_ciphersuites = 0;
  r->num_tls_extensions = 0;
  r->tls_sid_len = 0;
  r->tls_v = 0;
  r->tls_client_key_length = 0;

  memset(r->tls_len, 0, sizeof(r->tls_len));
  memset(r->tls_time, 0, sizeof(r->tls_time));
  memset(r->tls_type, 0, sizeof(r->tls_type));
  memset(r->ciphersuites, 0, sizeof(r->ciphersuites));
  memset(r->tls_extensions, 0, sizeof(r->tls_extensions));
  memset(r->tls_sid, 0, sizeof(r->tls_sid));
  memset(r->tls_random, 0, sizeof(r->tls_random));
}

/* free data associated with TLS */
void tls_record_delete(struct tls_information *r) {
  int i;
  for (i=0; i<r->num_tls_extensions; i++) {
    if (r->tls_extensions[i].data) {
      free(r->tls_extensions[i].data);
    }
  }
}


unsigned short raw_to_unsigned_short(const void *x) {
  unsigned short int y;
  const unsigned char *z = x;

  y = z[0];
  y *= 256;
  y += z[1];
  return y;
}

/*
void TLSClientKeyExchange_get_key_length(const void *x, int len, int version,
					 struct tls_information *r) {
  const unsigned char *y = x;

  if (r->tls_op > 1 || len < 32) {
    return ;
  }
  // SPDY was somehow getting here and causing havoc
  if (r->tls_client_key_length > 0) {
    return ;
  }
  // check for random encrypted handshake messages, not the best check
  //if (r->twin != NULL && r->twin->tls_client_key_length > 0) {
  //  return ;
  //}

  // SSL 3.0 uses a slightly different format
  //if (version == 2) {
  //  if (htons(*(const unsigned short *)(y-2))*8 < 8193) {
  //    r->tls_client_key_length = htons(*(const unsigned short *)(y-2))*8;
  //  }
  //  return ;
  //}

  // there must be a better check, but DH/EC sometimes uses 1/2 byte(s) for len
  if (len > 256 || ((int)*(const char *)y) <= 0) {
    if (htons(*(const unsigned short *)y)*8 < 8193) {
      r->tls_client_key_length = htons(*(const unsigned short *)y)*8;
    }
  } else {
    if (*(const char *)y*8 < 8193) {
      r->tls_client_key_length = *(const char *)y*8;
    }
  }
}
*/

void TLSClientHello_get_ciphersuites(const void *x, int len, 
				     struct tls_information *r) {
  unsigned int session_id_len;
  const unsigned char *y = x;
  unsigned short int cipher_suites_len;
  unsigned int i = 0;

  //  mem_print(x, len);
  //  fprintf(stderr, "TLS version %0x%0x\n", y[0], y[1]);

  if ((y[0] != 3) || (y[1] > 3)) {
    // fprintf(stderr, "warning: TLS version %0x%0x\n", y[0], y[1]);
    return;  
  }

  /* record the 32-byte Random field */
  memcpy(r->tls_random, y+2, 32); 

  y += 34;  /* skip over ProtocolVersion and Random */
  session_id_len = *y;

  len -= (session_id_len + 3);
  if (len < 0) {
    //fprintf(info, "error: TLS session ID too long\n"); 
    return;   /* error: session ID too long */
  }

  /* record the session id, if there is one */
  if (session_id_len) {
    r->tls_sid_len = session_id_len;
    memcpy(r->tls_sid, y+1, session_id_len); 
  }

  y += (session_id_len + 1);   /* skip over SessionID and SessionIDLen */
  // mem_print(y, 2);
  cipher_suites_len = raw_to_unsigned_short(y);
  if (len < cipher_suites_len) {
    //fprintf(info, "error: TLS ciphersuite list too long\n"); 
    return;   /* error: session ID too long */
  }
  y += 2;

  //  fprintf(output, "num_cipher_suites: %u\n", cipher_suites_len/2);
  r->num_ciphersuites = cipher_suites_len/2;
  r->num_ciphersuites = r->num_ciphersuites > MAX_CS ? MAX_CS : r->num_ciphersuites;
  for (i=0; i < r->num_ciphersuites; i++) {
    unsigned short int cs;
    
    cs = raw_to_unsigned_short(y);
    // fprintf(output, "ciphersuite: %x\n", cs);
    r->ciphersuites[i] = cs;
    y += 2;
  }
}

void TLSClientHello_get_extensions(const void *x, int len, 
				     struct tls_information *r) {
  unsigned int session_id_len, compression_method_len;
  const unsigned char *y = x;
  unsigned short int cipher_suites_len, extensions_len;
  unsigned int i = 0;


  len -= 4; // get handshake message length
  if ((y[0] != 3) || (y[1] > 3)) {
    return;  
  }

  y += 34;  /* skip over ProtocolVersion and Random */
  len -= 34;
  session_id_len = *y;

  len -= (session_id_len + 3);
  if (len < 0) {
    //fprintf(info, "error: TLS session ID too long\n"); 
    return;   /* error: session ID too long */
  }

  y += (session_id_len + 1);   /* skip over SessionID and SessionIDLen */

  cipher_suites_len = raw_to_unsigned_short(y);
  if (len < cipher_suites_len) {
    //fprintf(info, "error: TLS ciphersuite list too long\n"); 
    return;   /* error: session ID too long */
  }
  y += 2;
  len -= 2;

  // skip over ciphersuites
  y += cipher_suites_len;
  len -= cipher_suites_len;

  // skip over compression methods
  compression_method_len = *y;
  y += 1+compression_method_len;
  len -= 1+compression_method_len;

  // extensions length
  extensions_len = raw_to_unsigned_short(y);
  if (len < extensions_len) {
    //fprintf(info, "error: TLS extensions too long\n"); 
    return;   /* error: session ID too long */
  }
  y += 2;
  len -= 2;

  i = 0;
  while (len > 0) {
    r->tls_extensions[i].type = raw_to_unsigned_short(y);
    r->tls_extensions[i].length = raw_to_unsigned_short(y+2);
    // should check if length is reasonable?
    r->tls_extensions[i].data = malloc(r->tls_extensions[i].length);
    memcpy(r->tls_extensions[i].data, y+4, r->tls_extensions[i].length);

    r->num_tls_extensions += 1;
    i += 1;

    len -= 4;
    len -= raw_to_unsigned_short(y+2);
    y += 4 + raw_to_unsigned_short(y+2);
  }

  

}

void TLSServerHello_get_ciphersuite(const void *x, unsigned int len,
				    struct tls_information *r) {
  unsigned int session_id_len;
  const unsigned char *y = x;
  unsigned short int cs; 

  //  mem_print(x, len);

  if ((y[0] != 3) || (y[1] > 3)) {
    // fprintf(stderr, "warning: TLS version %0x%0x\n", y[0], y[1]);
    return;  
  }

  /* record the 32-byte Random field */
  memcpy(r->tls_random, y+2, 32); 

  y += 34;  /* skip over ProtocolVersion and Random */
  session_id_len = *y;
  // fprintf(output, "session_id_len: %u\n", session_id_len);
  if (session_id_len + 3 > len) {
    //fprintf(info, "error: TLS session ID too long\n"); 
    return;   /* error: session ID too long */
  }

  /* record the session id, if there is one */
  if (session_id_len) {
    r->tls_sid_len = session_id_len;
    memcpy(r->tls_sid, y+1, session_id_len); 
  }

  y += (session_id_len + 1);   /* skip over SessionID and SessionIDLen */
  // mem_print(y, 2);
  cs = raw_to_unsigned_short(y);
  // fprintf(output, "selected ciphersuite: %x\n", cs);
  r->num_ciphersuites = 1;
  r->ciphersuites[0] = cs;
}


unsigned int TLSHandshake_get_length(const struct TLSHandshake *H) {
  return H->lengthLo + ((unsigned int) H->lengthMid) * 0x100 
    + ((unsigned int) H->lengthHi) * 0x10000;
}

unsigned int tls_header_get_length(const struct tls_header *H) {
  return H->lengthLo + ((unsigned int) H->lengthMid) * 0x100;
}


char *tls_version_get_string(enum tls_version v) {
  switch(v) {
  case 1:
    return "sslv2";
    break;
  case 2:
    return "sslv3";
    break;
  case 3:
    return "tls1.0";
    break;
  case 4:
    return "tls1.1";
    break;
  case 5:
    return "tls1.2";
    break;
  case 0:
    ;
    break;
  }
  return "unknown";
}

unsigned char tls_version(const void *x) {
  const unsigned char *z = x;

  // printf("tls_version: ");  mem_print(x, 2);

  switch(z[0]) {
  case 3:
    switch(z[1]) {
    case 0:
      return tls_sslv3;
      break;
    case 1:
      return tls_tls1_0;
      break;
    case 2:
      return tls_tls1_1;
      break;
    case 3:
      return tls_tls1_2;
      break;
    }
    break;
  case 2:
    return tls_sslv2;
    break;
  default:
    ;
  } 
  return tls_unknown;
}

unsigned int packet_is_sslv2_hello(const void *data) {
  const unsigned char *d = data;
  unsigned char b[3];
  
  b[0] = d[0];
  b[1] = d[1];
  b[2] = d[2];

  if (b[0] & 0x80) {
    b[0] &= 0x7F;
    if (raw_to_unsigned_short(b) > 9) {
      if (b[2] == 0x01) {
	return tls_sslv2;
      }
    }    
  }

  return tls_unknown;
}

struct tls_information *
process_tls(const struct pcap_pkthdr *h, const void *start, int len, struct tls_information *r) {
  const struct tls_header *tls;
  unsigned int tls_len;
  unsigned int levels = 0;

  /* currently skipping SSLv2 */

  while (len > 0) {
    tls = start;
    tls_len = tls_header_get_length(tls);
    if (tls->ContentType == application_data) {
      levels++;

      /* sanity check version number */
      if ((tls->ProtocolVersionMajor != 3) || (tls->ProtocolVersionMinor > 3)) {
	return NULL;
      }
      r->tls_v = tls_version(&tls->ProtocolVersionMajor);

    } else if (tls->ContentType == handshake) {
      if (tls->Handshake.HandshakeType == client_hello) {
	
	TLSClientHello_get_ciphersuites(&tls->Handshake.body, tls_len, r);
	TLSClientHello_get_extensions(&tls->Handshake.body, tls_len, r);

      } else if (tls->Handshake.HandshakeType == server_hello) {

	TLSServerHello_get_ciphersuite(&tls->Handshake.body, tls_len, r);

      } else if (tls->Handshake.HandshakeType == client_key_exchange) {

	//	TLSClientKeyExchange_get_key_length(&tls->Handshake.body, tls_len, tls_version(&tls->ProtocolVersionMajor), r);
	if (r->tls_client_key_length == 0) {
	  r->tls_client_key_length = (unsigned int)tls->Handshake.lengthLo*8 + 
	    (unsigned int)tls->Handshake.lengthMid*8*256 + 
	    (unsigned int)tls->Handshake.lengthHi*8*256*256;
	  if (r->tls_client_key_length > 8193) {
	    r->tls_client_key_length = 0;
	  }
	}

      } if (((tls->Handshake.HandshakeType > 2) & 
	     (tls->Handshake.HandshakeType < 11)) ||
	    ((tls->Handshake.HandshakeType > 16) & 
	     (tls->Handshake.HandshakeType < 20)) ||
	    (tls->Handshake.HandshakeType > 20)) {
	
	/*
	 * we encountered an unknown handshaketype, so this packet is
	 * not actually a TLS handshake, so we bail on decoding it
	 */
	return NULL;
      }

      if (r->tls_op < MAX_NUM_RCD_LEN) {
	r->tls_type[r->tls_op].handshake = tls->Handshake.HandshakeType;
      }      
    } else if (tls->ContentType != change_cipher_spec || 
	       tls->ContentType != alert) {
      
      /* 
       * we encountered an unknown contenttype, so this is not
       * actually a TLS record, so we bail on decoding it
       */      
      return NULL;
    }

    /* record TLS record lengths and arrival times */
    if (r->tls_op < MAX_NUM_RCD_LEN) {
      r->tls_type[r->tls_op].content = tls->ContentType;
      r->tls_len[r->tls_op] = tls_len;
      r->tls_time[r->tls_op] = h->ts;
    }

    /* increment TLS record count in tls_information */
    r->tls_op++;

    tls_len += 5; /* advance over header */
    start += tls_len;
    len -= tls_len;
  }

  return NULL;
}
