/*
 *@BEGIN LICENSE
 *
 * PSI4: an ab initio quantum chemistry software package
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *@END LICENSE
 */

/*! \file
    \ingroup DPD
    \brief Enter brief description of file here 
*/
#include <cstdio>
#include <cmath>
#include <cstring>
#include <libqt/qt.h>
#include "dpd.h"

namespace psi {

/* dpd_buf4_scmcopy(): Copies an existing four-index dpdbuf4 into another 
** file and multiplies it by a scalar at the same time.
**
** Arguments:
**   dpdbuf4 *InBuf: A pointer to the given dpd buffer.
**   int outfilenum: The PSI unit number for the new buffer.
**   char *label: A string labelling for this buffer.
**   double alpha: A scalar.
**
** NB: The buffer and file pq/rs parameters are assumed to be
** identical for the copy, obviously.  Hence, the anti flag must be off.
**
*/

int dpd_buf4_scmcopy(dpdbuf4 *InBuf, int outfilenum, const char *label, double alpha)
{
  int h, row, col, rowtot, coltot, all_buf_irrep;
  int nbuckets, incore, n, size;
  long int memoryd, rows_per_bucket, rows_left;
  dpdbuf4 OutBuf;

  all_buf_irrep = InBuf->file.my_irrep;

  dpd_buf4_init(&OutBuf, outfilenum, InBuf->file.my_irrep, InBuf->params->pqnum,
		InBuf->params->rsnum, InBuf->params->pqnum, 
                InBuf->params->rsnum, 0, label);

  for(h=0; h < InBuf->params->nirreps; h++) {

    /* select in-core or out-of-core algorithm */
    memoryd = dpd_memfree()/2; /* use half the memory for each buf4 */
    if(InBuf->params->rowtot[h] && InBuf->params->coltot[h^all_buf_irrep]) {

      rows_per_bucket = memoryd/InBuf->params->coltot[h^all_buf_irrep];

      /* enough memory for the whole matrix? */
      if(rows_per_bucket > InBuf->params->rowtot[h]) 
	rows_per_bucket = InBuf->params->rowtot[h]; 

      if(!rows_per_bucket) dpd_error("buf4_scmcopy: Not enough memory for one row!", stderr);

      nbuckets = (int) ceil(((double) InBuf->params->rowtot[h])/((double) rows_per_bucket));

      rows_left = InBuf->params->rowtot[h] % rows_per_bucket;

      incore = 1;
      if(nbuckets > 1) {
	incore = 0;
#if DPD_DEBUG
        fprintf(stderr, "buf4_scmcopy: memory information.\n");
        fprintf(stderr, "buf4_scmcopy: rowtot[%d] = %d.\n", h, InBuf->params->rowtot[h]);
	fprintf(stderr, "buf4_scmcopy: nbuckets = %d\n", nbuckets);
	fprintf(stderr, "buf4_scmcopy: rows_per_bucket = %d\n", rows_per_bucket);
	fprintf(stderr, "buf4_scmcopy: rows_left = %d\n", rows_left);
	fprintf(stderr, "buf4_scmcopy: out-of-core algorithm used\n");
#endif
      }

    }
    else incore = 1;

    if(incore) {
      dpd_buf4_mat_irrep_init(InBuf, h);
      dpd_buf4_mat_irrep_rd(InBuf, h);

      dpd_buf4_mat_irrep_init(&OutBuf, h);

      rowtot = InBuf->params->rowtot[h];
      coltot = InBuf->params->coltot[h^all_buf_irrep];
      size = rowtot*coltot;

      if(rowtot && coltot) {
	memcpy((void *) &(OutBuf.matrix[h][0][0]),
	       (const void *) &(InBuf->matrix[h][0][0]),
	       ((long) sizeof(double))*size);
	C_DSCAL(size, alpha, &(OutBuf.matrix[h][0][0]), 1);
      }

      dpd_buf4_mat_irrep_wrt(&OutBuf, h);

      dpd_buf4_mat_irrep_close(&OutBuf, h);
      dpd_buf4_mat_irrep_close(InBuf, h);
    }
    else {

      dpd_buf4_mat_irrep_init_block(InBuf, h, rows_per_bucket);
      dpd_buf4_mat_irrep_init_block(&OutBuf, h, rows_per_bucket);

      coltot = InBuf->params->coltot[h^all_buf_irrep];
      size = ((long) rows_per_bucket)*((long) coltot);

      for(n=0; n < (rows_left ? nbuckets-1 : nbuckets); n++) {

	dpd_buf4_mat_irrep_rd_block(InBuf, h, n*rows_per_bucket, rows_per_bucket);

	memcpy((void *) &(OutBuf.matrix[h][0][0]), (const void *) &(InBuf->matrix[h][0][0]), 
	       ((long) sizeof(double))*size);
	C_DSCAL(size, alpha, &(OutBuf.matrix[h][0][0]), 1);

	dpd_buf4_mat_irrep_wrt_block(&OutBuf, h, n*rows_per_bucket, rows_per_bucket);
      }
      if(rows_left) {

	size = ((long) rows_left) * ((long) coltot);

	dpd_buf4_mat_irrep_rd_block(InBuf, h, n*rows_per_bucket, rows_left);

	memcpy((void *) &(OutBuf.matrix[h][0][0]), (const void *) &(InBuf->matrix[h][0][0]), 
	       ((long) sizeof(double))*size);
	C_DSCAL(size, alpha, &(OutBuf.matrix[h][0][0]), 1);

	dpd_buf4_mat_irrep_wrt_block(&OutBuf, h, n*rows_per_bucket, rows_left);
      }

      dpd_buf4_mat_irrep_close_block(InBuf, h, rows_per_bucket);
      dpd_buf4_mat_irrep_close_block(&OutBuf, h, rows_per_bucket);
    }
  }

  dpd_buf4_close(&OutBuf);

  return 0;
}

}
