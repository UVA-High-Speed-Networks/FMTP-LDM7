#include "config.h"

#include "geminc.h"
#include "gemprm.h"
#include "ctbcmn.h"

#include <stdio.h>

void ctb_g2rdlvl ( char *tbname, G2lvls *lvltbl, int *iret ) 
/************************************************************************
 * ctb_g2rdlvl								*
 *									*
 * This routine will read a GRIB2 vertical coordinate level/layer       *
 * table into an array of structures.	                                *
 * The table is allocated locally and a pointer to the new table is     *
 * passed back to the user in argument lvltbl.  The user is responsible *
 * for freeing this memory, when the table is no longer needed, by      *
 * free(lvltbl.info)                                                    *
 *									*
 * ctb_g2rdlvl ( tbname, lvltbl, iret )				        *
 *									*
 * Input parameters:							*
 *	*tbname char    Filename of the table to read                   *
 *									*
 * Output parameters:							*
 *	*lvltbl G2lvls  Pointer to list of table entries                *
 *	*iret   int     Return code			                *
 *                          0 = Successful                              *
 *                         -1 = Could not open                          *
 *                         -2 = Could not get count of of table entries *
 *                        -52 = Memory allocation failure (G_NMEMRY)    *
 **									*
 * Log:									*
 * S. Gilbert/NCEP	 11/04	Modified from ctb_g2rdcntr to read a    *
 *                              GRIB2 level/layer Table.                *
 * S. Emmerson/Unidata   12/15  Added check for malloc() returning NULL *
 ***********************************************************************/
{
        FILE     *fp = NULL;
        int      n, blen, id1, id2, scale, nr, ier;
        char     buffer[256]; 
        char     name[34], abbrev[5], unit[21];

/*---------------------------------------------------------------------*/
	*iret = G_NORMAL;

        /*
         *  Open the table. If not found return an error.
         */

        fp = cfl_tbop( tbname, "grid", &ier);
        if ( fp == NULL  ||  ier != 0 )  {
            if (fp)
                fclose(fp);
            *iret = -1;
            return;
        }

        cfl_tbnr(fp, &nr, &ier);
        if ( ier != 0 || nr == 0 ) {
            *iret = -2;
            cfl_clos(fp, &ier);
            return;
        }

        lvltbl->info = (G2level *)calloc((size_t)nr, sizeof(G2level));
        if (lvltbl->info == NULL) {
            *iret = G_NMEMRY;
            cfl_clos(fp, &ier);
            return;
        }
        lvltbl->nlines = nr;

        n  = 0;
        while ( n < nr ) {

            cfl_trln( fp, 256, buffer, &ier );
            if ( ier != 0 ) {
                free(lvltbl->info);
                break;
            }

            cst_lstr (  buffer, &blen, &ier );

            int numAssigned = sscanf( buffer, "%11d %11d %33c %20c %4s %11d",
                            &id1, &id2, name, unit, abbrev, &scale);

            if (numAssigned != 6) {
                log_add("Couldn't decode 6 fields from entry %d, \"%s\"", n,
                        buffer);
                *iret = -2;
            }
            else {
                name[33] = '\0';
                unit[20] = '\0';
                abbrev[4] = '\0';

                lvltbl->info[n].id1=id1;
                lvltbl->info[n].id2=id2;
                strcpy(lvltbl->info[n].name,    name);
                strcpy(lvltbl->info[n].unit,    unit);
                strcpy(lvltbl->info[n].abbrev,  abbrev);
                lvltbl->info[n].scale=scale;
            }

            n++;
        }

        cfl_clos(fp, &ier);

}
