/*
  eXosip - This is the eXtended osip library.
  Copyright (C) 2002, 2003  Aymeric MOIZARD  - jack@atosc.org
  
  eXosip is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  eXosip is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#ifdef ENABLE_MPATROL
#include <mpatrol.h>
#endif

#include <eXosip.h>

char   *localip;

int eXosip_sdp_accept_audio_codec(sdp_negotiation_ctx_t *context,
			      char *port, char *number_of_port,
			      int audio_qty, char *payload)
{    
  if (0==strncmp(payload,"0",1)||0==strncmp(payload,"8",1)
      ||0==strncmp(payload,"3",1))
    return 0;
  return -1;
}
  
int eXosip_sdp_accept_video_codec(sdp_negotiation_ctx_t *context,
			      char *port, char *number_of_port,
			      int video_qty, char *payload)
{
  return -1;
}

int eXosip_sdp_accept_other_codec(sdp_negotiation_ctx_t *context,
			      char *type, char *port,
			      char *number_of_port, char *payload)
{
  /* ... */
  return -1;
}

char *eXosip_sdp_get_audio_port(sdp_negotiation_ctx_t *context, int pos_media)
{
  return osip_strdup("34954");
}

int eXosip_sdp_negotiation_init()
{

  int i = sdp_negotiation_init();
  if (i!=0) {
    return -1;
  }
  sdp_negotiation_set_o_username(osip_strdup("userX"));
  sdp_negotiation_set_o_session_id(osip_strdup("20000001"));
  sdp_negotiation_set_o_session_version(osip_strdup("20000001"));
  sdp_negotiation_set_o_nettype(osip_strdup("IN"));
  sdp_negotiation_set_o_addrtype(osip_strdup("IP4"));
  sdp_negotiation_set_o_addr(osip_strdup(localip));
  
  sdp_negotiation_set_c_nettype(osip_strdup("IN"));
  sdp_negotiation_set_c_addrtype(osip_strdup("IP4"));
  sdp_negotiation_set_c_addr(osip_strdup(localip));
  
  /* ALL CODEC MUST SHARE THE SAME "C=" line and proto as the media 
     will appear on the same "m" line... */
  sdp_negotiation_add_support_for_audio_codec(osip_strdup("0"),
					 NULL,
					 osip_strdup("RTP/AVP"),
					 NULL, NULL, NULL,
					 NULL,NULL,
					 osip_strdup("0 PCMU/8000"));
  sdp_negotiation_add_support_for_audio_codec(osip_strdup("3"),
					 NULL,
					 osip_strdup("RTP/AVP"),
					 NULL, NULL, NULL,
					 NULL,NULL,
					 osip_strdup("3 GSM/8000"));
  sdp_negotiation_add_support_for_audio_codec(osip_strdup("8"),
					 NULL,
					 osip_strdup("RTP/AVP"),
					 NULL, NULL, NULL,
					 NULL,NULL,
					 osip_strdup("8 PCMA/8000"));
  
  sdp_negotiation_set_fcn_accept_audio_codec(&eXosip_sdp_accept_audio_codec);
  sdp_negotiation_set_fcn_accept_video_codec(&eXosip_sdp_accept_video_codec);
  
  sdp_negotiation_set_fcn_accept_other_codec(&eXosip_sdp_accept_other_codec);
  sdp_negotiation_set_fcn_get_audio_port(&eXosip_sdp_get_audio_port);

  return 0;
}

sdp_message_t *
eXosip_get_local_sdp_info(osip_transaction_t *invite_tr)
{
  osip_content_type_t *ctt;
  osip_mime_version_t *mv;
  osip_message_t *message;
  sdp_message_t *sdp;
  char  *oldbody;
  int pos;

  if (invite_tr->ctx_type == IST)
    message = invite_tr->last_response;
  else if (invite_tr->ctx_type == ICT)
    message = invite_tr->orig_request;
  else return NULL; /* BUG -> NOT AN INVITE TRANSACTION!! */

  if (message==NULL) return NULL;

  /* get content-type info */
  ctt = osip_parser_get_content_type(message);
  mv  = osip_parser_get_mime_version(message);
  if (mv==NULL && ctt==NULL)
    return NULL; /* previous message was not correct or empty */
  if (mv!=NULL)
    {
      /* look for the SDP body */
      /* ... */
    }
  else if (ctt!=NULL)
    {
      if (ctt->type==NULL || ctt->subtype==NULL)
	/* it can be application/sdp or mime... */
	return NULL;
      if (strcmp(ctt->type, "application")!=0 ||
	  strcmp(ctt->subtype, "sdp")!=0 )
	{ return NULL; }
    }
  
  pos=0;
  while (!osip_list_eol(invite_tr->last_response->bodies, pos))
    {
      int i;
      oldbody = osip_list_get(invite_tr->last_response->bodies, pos);
      pos++;
      sdp_message_init(&sdp);
      i = sdp_message_parse(sdp,oldbody);
      if (i==0) return sdp;
      sdp_message_free(sdp);
      sdp = NULL;
    }
  return NULL;
}

