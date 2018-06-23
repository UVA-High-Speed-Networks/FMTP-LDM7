#include "config.h"

#include "geminc.h"
#include "gemprm.h"
#include "ctbcmn.h"

#include <stdio.h>

void ctb_g2rdcntr ( char *tbname, G2wmocntrs *cntrtbl, int *iret ) 
/************************************************************************
 * ctb_g2rdcntr								*
 *									*
 * This routine will read a WMO originating Center table into an array  *
 * of structures.	                                                *
 * The table is allocated locally and a pointer to the new table is     *
 * passed back to the user in argument cntrtbl.  The user is resposible *
 * for freeing this memory, when the table is no longer needed, by      *
 * free(cntrtbl.info)                                                   *
 *									*
 * ctb_g2rdcntr ( tbname, cntrtbl, iret )				*
 *									*
 * Input parameters:							*
 *	*tbname		char		Filename of the table to read   *
 *									*
 * Output parameters:							*
 *	*cntrtbl	G2wmocenter	Pointer to list of table entries*
 *	*iret		int		Return code			*
 *                                        0 = Successful                *
 *                                       -1 = Could not open            *
 *                                       -1 = Could not get count of    *
 *                                            of teble entries.         *
 **									*
 * Log:									*
 * m.gamazaychikov/SAIC	 5/03						*
 * m.gamazaychikov/SAIC	 5/03	added parameter pdtnmbr to the table	*
 * M. Li/SAIC		 4/04	added hzremap, and direction		*
 * S. Gilbert/NCEP	 11/04	Modified from ctb_g2read to read a WMO  *
 *                              Originating Center Table.               *
 ***********************************************************************/
{
        FILE     *fp = NULL;
        int      n, blen, id, nr, ier;
        char     buffer[256]; 
        char     name[65], abbrev[9];

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

        cntrtbl->info = (G2wmocenter *)calloc((size_t)nr, sizeof(G2wmocenter));
        cntrtbl->nlines = nr;

        n  = 0;
        while ( n < nr ) {

            cfl_trln( fp, 256, buffer, &ier );
            if ( ier != 0 ) break;

	    cst_lstr (  buffer, &blen, &ier );

            int numAssigned = sscanf( buffer, "%10d %64c %8s",
                            &id, name, abbrev);

            if (numAssigned != 3) {
                log_add("Couldn't decode 3 fields from entry %d", n);
                *iret = -2;
            }
            else {
                name[64] = '\0';
                abbrev[8] = '\0';

                cntrtbl->info[n].id=id;
                strcpy(cntrtbl->info[n].name,    name);
                strcpy(cntrtbl->info[n].abbrev,  abbrev);
            }

            n++;
        }

        cfl_clos(fp, &ier);

}
