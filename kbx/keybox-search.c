/* keybox-search.c - Search operations
 *	Copyright (C) 2001 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG.
 *
 * GnuPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GnuPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "keybox-defs.h"

static ulong
get32 (const byte *buffer)
{
  ulong a;
  a =  *buffer << 24;
  a |= buffer[1] << 16;
  a |= buffer[2] << 8;
  a |= buffer[3];
  return a;
}

static ulong
get16 (const byte *buffer)
{
  ulong a;
  a =  *buffer << 8;
  a |= buffer[1];
  return a;
}



static int
blob_get_type (KEYBOXBLOB blob)
{
  const unsigned char *buffer;
  size_t length;

  buffer = _keybox_get_blob_image (blob, &length);
  if (length < 40)
    return -1; /* blob too short */

  return buffer[4];
}


static int
blob_cmp_sn (KEYBOXBLOB blob, const unsigned char *sn)
{
  size_t snlen;
  const unsigned char *buffer;
  size_t length;
  size_t pos, off;
  size_t nkeys, keyinfolen;
  size_t nserial;

  snlen = (sn[0] << 24) | (sn[1] << 16) | (sn[2] << 8) | sn[3];
  sn += 4;

  buffer = _keybox_get_blob_image (blob, &length);
  if (length < 40)
    return 0; /* blob too short */

  /*keys*/
  nkeys = get16 (buffer + 16);
  keyinfolen = get16 (buffer + 18 );
  if (keyinfolen < 28)
    return 0; /* invalid blob */
  pos = 20 + keyinfolen*nkeys;
  if (pos+2 > length)
    return 0; /* out of bounds */

  /*serial*/
  nserial = get16 (buffer+pos); 
  off = pos + 2;
  if (off+nserial > length)
    return 0; /* out of bounds */

  return nserial == snlen && !memcmp (buffer+off, sn, snlen);
}

static int
blob_cmp_name (KEYBOXBLOB blob, int idx, const char *name, size_t namelen)
{
  const unsigned char *buffer;
  size_t length;
  size_t pos, off, len;
  size_t nkeys, keyinfolen;
  size_t nuids, uidinfolen;
  size_t nserial;

  buffer = _keybox_get_blob_image (blob, &length);
  if (length < 40)
    return 0; /* blob too short */

  /*keys*/
  nkeys = get16 (buffer + 16);
  keyinfolen = get16 (buffer + 18 );
  if (keyinfolen < 28)
    return 0; /* invalid blob */
  pos = 20 + keyinfolen*nkeys;
  if (pos+2 > length)
    return 0; /* out of bounds */

  /*serial*/
  nserial = get16 (buffer+pos); 
  pos += 2 + nserial;
  if (pos+4 > length)
    return 0; /* out of bounds */

  /* user ids*/
  nuids = get16 (buffer + pos);  pos += 2;
  uidinfolen = get16 (buffer + pos);  pos += 2;
  if (uidinfolen < 12 /* should add a: || nuidinfolen > MAX_UIDINFOLEN */)
    return 0; /* invalid blob */
  if (pos + uidinfolen*nuids > length)
    return 0; /* out of bounds */

  if (idx > nuids)
    return 0; /* no user ID with that idx */
  pos += idx*uidinfolen;
  off = get32 (buffer+pos);
  len = get32 (buffer+pos+4);
  if (off+len > length)
    return 0; /* out of bounds */
  if (len < 2)
    return 0; /* empty name or 0 not stored */
  len--;
  
  return len == namelen && !memcmp (buffer+off, name, len);
}




/*
  The has_foo functions are used as helpers for search 
*/
#if 0
static int
has_short_kid (KEYBOXBLOB blob, u32 kid)
{
  return 0;
}

static int
has_long_kid (KEYBOXBLOB blob, u32 *kid)
{
  return 0;
}
#endif

static int
has_fingerprint (KEYBOXBLOB blob, const unsigned char *fpr)
{
  return 0;
}


static int
has_issuer (KEYBOXBLOB blob, const char *name)
{
  size_t namelen;

  return_val_if_fail (name, 0);

  if (blob_get_type (blob) != BLOBTYPE_X509)
    return 0;

  namelen = strlen (name);
  return blob_cmp_name (blob, 0 /* issuer */, name, namelen);
}

static int
has_issuer_sn (KEYBOXBLOB blob, const char *name, const unsigned char *sn)
{
  size_t namelen;

  return_val_if_fail (name, 0);
  return_val_if_fail (sn, 0);

  if (blob_get_type (blob) != BLOBTYPE_X509)
    return 0;

  namelen = strlen (name);
  
  return (blob_cmp_sn (blob, sn)
          && blob_cmp_name (blob, 0 /* issuer */, name, namelen));
}



/*

  The search API

*/

int 
keybox_search_reset (KEYBOX_HANDLE hd)
{
  if (!hd)
    return KEYBOX_Invalid_Value;

  if (hd->found.blob)
    {
      _keybox_release_blob (hd->found.blob);
      hd->found.blob = NULL;
    }

  if (hd->fp)
    {
      fclose (hd->fp);
      hd->fp = NULL;
    }
  hd->error = 0;
  hd->eof = 0;
  return 0;   
}

int 
keybox_search (KEYBOX_HANDLE hd, KEYBOX_SEARCH_DESC *desc, size_t ndesc)
{
  int rc;
  size_t n;
  int need_words, any_skip;
  KEYBOXBLOB blob = NULL;

  if (!hd)
    return KEYBOX_Invalid_Value;

  /* clear last found result */
  if (hd->found.blob)
    {
      _keybox_release_blob (hd->found.blob);
      hd->found.blob = NULL;
    }

  if (hd->error)  
    return hd->error; /* still in error state */
  if (hd->eof)  
    return -1; /* still EOF */

  /* figure out what information we need */
  need_words = any_skip = 0;
  for (n=0; n < ndesc; n++) 
    {
      switch (desc[n].mode) 
        {
        case KEYDB_SEARCH_MODE_WORDS: 
          need_words = 1;
          break;
        case KEYDB_SEARCH_MODE_FIRST:
          /* always restart the search in this mode */
          keybox_search_reset (hd);
          break;
        default:
          break;
	}
      if (desc[n].skipfnc) 
        any_skip = 1;
    }

  if (!hd->fp)
    {
      hd->fp = fopen (hd->kb->fname, "rb");
      if (!hd->fp)
          return (hd->error = KEYBOX_File_Open_Error);
    }


  for (;;)
    {
      _keybox_release_blob (blob); blob = NULL;
      rc = _keybox_read_blob (&blob, hd->fp);
      if (rc)
        break;

      for (n=0; n < ndesc; n++) 
        {
          switch (desc[n].mode)
            {
            case KEYDB_SEARCH_MODE_NONE: 
              never_reached ();
              break;
            case KEYDB_SEARCH_MODE_EXACT: 
            case KEYDB_SEARCH_MODE_SUBSTR:
            case KEYDB_SEARCH_MODE_MAIL:
            case KEYDB_SEARCH_MODE_MAILSUB:
            case KEYDB_SEARCH_MODE_MAILEND:
            case KEYDB_SEARCH_MODE_WORDS: 
              never_reached (); /* not yet implemented */
              break;
            case KEYDB_SEARCH_MODE_ISSUER:
              if (has_issuer (blob, desc[n].u.name))
                goto found;
              break;
            case KEYDB_SEARCH_MODE_ISSUER_SN:
              if (has_issuer_sn (blob, desc[n].u.name, desc[n].sn))
                goto found;
              break;
            case KEYDB_SEARCH_MODE_SHORT_KID: 
/*                if (has_short_kid (blob, desc[n].u.kid[1])) */
/*                  goto found; */
              break;
            case KEYDB_SEARCH_MODE_LONG_KID:
/*                if (has_long_kid (blob, desc[n].u.kid)) */
/*                  goto found; */
              break;
            case KEYDB_SEARCH_MODE_FPR:
              if (has_fingerprint (blob, desc[n].u.fpr))
                goto found;
              break;
            case KEYDB_SEARCH_MODE_FIRST: 
              goto found;
              break;
            case KEYDB_SEARCH_MODE_NEXT: 
              goto found;
              break;
            default: 
              rc = KEYBOX_Invalid_Value;
              goto found;
            }
	}
      continue;
    found:  
      for (n=any_skip?0:ndesc; n < ndesc; n++) 
        {
/*            if (desc[n].skipfnc */
/*                && desc[n].skipfnc (desc[n].skipfncvalue, aki)) */
/*              break; */
        }
      if (n == ndesc)
        break; /* got it */
    }
  
  if (!rc)
    {
      hd->found.blob = blob;
    }
  else if (rc == -1)
    {
      _keybox_release_blob (blob);
      hd->eof = 1;
    }
  else 
    {
      _keybox_release_blob (blob);
      hd->error = rc;
    }

  return rc;
}




/*
   Functions to return a certificate or a keyblock.  To be used after
   a successful search operation.
*/
#ifdef KEYBOX_WITH_X509
/*
  Return the last found cert.  Caller must free it.
 */
int
keybox_get_cert (KEYBOX_HANDLE hd, KsbaCert *r_cert)
{
  const unsigned char *buffer;
  size_t length;
  size_t cert_off, cert_len;
  KsbaReader reader = NULL;
  KsbaCert cert = NULL;
  int rc;

  if (!hd)
    return KEYBOX_Invalid_Value;
  if (!hd->found.blob)
    return KEYBOX_Nothing_Found;

  if (blob_get_type (hd->found.blob) != BLOBTYPE_X509)
    return KEYBOX_Wrong_Blob_Type;

  buffer = _keybox_get_blob_image (hd->found.blob, &length);
  if (length < 40)
    return KEYBOX_Blob_Too_Short;
  cert_off = get32 (buffer+8);
  cert_len = get32 (buffer+12);
  if (cert_off+cert_len > length)
    return KEYBOX_Blob_Too_Short;

  reader = ksba_reader_new ();
  if (!reader)
    return KEYBOX_Out_Of_Core;
  rc = ksba_reader_set_mem (reader, buffer+cert_off, cert_len);
  if (rc)
    {
      ksba_reader_release (reader);
      /* fixme: need to map the error codes */
      return KEYBOX_General_Error;
    }

  cert = ksba_cert_new ();
  if (!cert)
    {
      ksba_reader_release (reader);
      return KEYBOX_Out_Of_Core;
    }

  rc = ksba_cert_read_der (cert, reader);
  if (rc)
    {
      ksba_cert_release (cert);
      ksba_reader_release (reader);
      /* fixme: need to map the error codes */
      return KEYBOX_General_Error;
    }

  *r_cert = cert;
  ksba_reader_release (reader);
  return 0;
}

#endif /*KEYBOX_WITH_X509*/
