/* certcheck.c - check one certificate
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h> 
#include <time.h>
#include <assert.h>

#include <gcrypt.h>
#include <ksba.h>

#include "gpgsm.h"
#include "keydb.h"
#include "i18n.h"

static int
do_encode_md (GCRY_MD_HD md, int algo, size_t len, unsigned nbits,
	      const byte *asn, size_t asnlen, GCRY_MPI *r_val)
{
  int nframe = (nbits+7) / 8;
  byte *frame;
  int i, n;
  
  if ( len + asnlen + 4  > nframe )
    {
      log_error ("can't encode a %d bit MD into a %d bits frame\n",
                 (int)(len*8), (int)nbits);
      return GPGSM_Internal_Error;
    }
  
  /* We encode the MD in this way:
   *
   *	   0  A PAD(n bytes)   0  ASN(asnlen bytes)  MD(len bytes)
   *
   * PAD consists of FF bytes.
   */
  frame = xtrymalloc (nframe);
  if (!frame)
    return GPGSM_Out_Of_Core;
  n = 0;
  frame[n++] = 0;
  frame[n++] = 1; /* block type */
  i = nframe - len - asnlen -3 ;
  assert ( i > 1 );
  memset ( frame+n, 0xff, i ); n += i;
  frame[n++] = 0;
  memcpy ( frame+n, asn, asnlen ); n += asnlen;
  memcpy ( frame+n, gcry_md_read(md, algo), len ); n += len;
  assert ( n == nframe );
  gcry_mpi_scan (r_val, GCRYMPI_FMT_USG, frame, &nframe);
  xfree (frame);
  return 0;
}


/*
  Check the signature on CERT using the ISSUER-CERT.  This function
  does only test the cryptographic signature and nothing else.  It is
  assumed that the ISSUER_CERT is valid. */
int
gpgsm_check_cert_sig (KsbaCert issuer_cert, KsbaCert cert)
{
  /* OID for MD5 as defined in PKCS#1 (rfc2313) */
  static byte asn[18] = /* Object ID is 1.2.840.113549.2.5 (md5) */
  { 0x30, 0x20, 0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86, 0x48,
    0x86, 0xf7, 0x0d, 0x02, 0x05, 0x05, 0x00, 0x04, 0x10
  };

  GCRY_MD_HD md;
  int rc, algo;
  GCRY_MPI frame;
  char *p;
  GCRY_SEXP s_sig, s_hash, s_pkey;

  algo = ksba_cert_get_digest_algo (cert);
  md = gcry_md_open (algo, 0);
  if (!md)
    {
      log_error ("md_open failed: %s\n", gcry_strerror (-1));
      return GPGSM_General_Error;
    }

  rc = ksba_cert_hash (cert, 1, HASH_FNC, md);
  if (rc)
    {
      log_error ("ksba_cert_hash failed: %s\n", ksba_strerror (rc));
      gcry_md_close (md);
      return map_ksba_err (rc);
    }
  gcry_md_final (md);

  p = ksba_cert_get_sig_val (cert); /* fixme: check p*/
  if (DBG_X509)
    log_debug ("signature: %s\n", p);

  rc = gcry_sexp_sscan ( &s_sig, NULL, p, strlen(p));
  if (rc)
    {
      log_error ("gcry_sexp_scan failed: %s\n", gcry_strerror (rc));
      return map_gcry_err (rc);
    }
  /*gcry_sexp_dump (s_sig);*/


  /* FIXME: need to map the algo to the ASN OID - we assume a fixed
     one for now */
  rc = do_encode_md (md, algo, 16, 2048, asn, DIM(asn), &frame);
  if (rc)
    {
      /* fixme: clean up some things */
      return rc;
    }
  /* put hash into the S-Exp s_hash */
  if ( gcry_sexp_build (&s_hash, NULL, "%m", frame) )
    BUG ();
  /*fputs ("hash:\n", stderr); gcry_sexp_dump (s_hash);*/

  p = ksba_cert_get_public_key (issuer_cert);
  if (DBG_X509)
    log_debug ("issuer public key: %s\n", p);

  rc = gcry_sexp_sscan ( &s_pkey, NULL, p, strlen(p));
  if (rc)
    {
      log_error ("gcry_sexp_scan failed: %s\n", gcry_strerror (rc));
      return map_gcry_err (rc);
    }
  /*gcry_sexp_dump (s_pkey);*/
  
  rc = gcry_pk_verify (s_sig, s_hash, s_pkey);
  if (DBG_CRYPTO)
      log_debug ("gcry_pk_verify: %s\n", gcry_strerror (rc));
  return map_gcry_err (rc);
}

