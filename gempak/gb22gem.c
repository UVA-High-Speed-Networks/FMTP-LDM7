#include "config.h"

#include "gb2def.h"
#include "log.h"

#include <string.h>

void gb2_2gem( Gribmsg *cmsg, Geminfo *gem , char **tbls, int *iret )
/************************************************************************
 * gb2_2gem								*
 *									*
 * This function converts GRIB2 Product Definition info and             *
 * Grid Definition info to GEMPAK header info.                          *
 *									*
 * gb2_2gem ( cmsg, gem, iret )				         	*
 *									*
 * Output parameters:							*
 *      *cmsg        struct gribmsg     current GRIB field              *
 *	*ivers		int		GRIB version number		*
 *	*iret		int		Return code			*
 *                                        0 = Successful                *
 *                                      -34 = Could not process GRIB2   *
 *                                            message                   *
 **									*
 * Log:									*
 * S. Gilbert/NCEP		 11/2004				*
 * S. Emmerson/Unidata           10/2018                                *
 *     Unknown PDTN when decoding time is no longer an error            *
 ***********************************************************************/
{
/*---------------------------------------------------------------------*/
    int iaccm, cntrid, ier, itmp, scal;
    char gdattm1[DTTMSZ];
    static char gdattm2[DTTMSZ]="                    ";
    char wmocntr[8];
    struct gribfield *tg;
    float missng;

    *iret = 0;
    tg=cmsg->gfld;

/*    printf("gem:%s:%s:\n",*(tbls+0),tbls[0]);
    printf("gem:%s:%s:\n",*(tbls+1),tbls[1]);
    printf("gem:%s:%s:\n",*(tbls+2),tbls[2]);
    printf("gem:%s:%s:\n",*(tbls+3),tbls[3]);
    printf("gem:%s:%s:\n",*(tbls+4),tbls[4]);
*/

    /*
     *    Get Originating Center from wmocenter.tbl
     */
    cntrid = tg->idsect[0];
    gb2_gtcntr( cntrid, tbls[4], wmocntr, &ier);
    if ( ier != 0 ) {
        char    msg[132];

        (void)snprintf(msg, sizeof(msg),
                "Couldn't find originating center %d in table \"%s\"",
                cntrid, tbls[4] ? tbls[4] : "wmocenter.tbl");
        msg[sizeof(msg)-1] = 0;
        ER_WMSG("GB", &ier, msg, &itmp, 2, 1);
    }
    cst_uclc(wmocntr, cmsg->origcntr, &ier);
    
    /*
     *    Convert Date/Time information
     */
    gb2_ftim ( tg, gdattm1, &iaccm, iret );
    if (*iret == -27)
        *iret = 0; // Not really an error

    strncpy( gem->gdattm1, gdattm1, DTTMSZ-1)[DTTMSZ-1] = 0;
    strncpy( gem->gdattm2, gdattm2, DTTMSZ-1)[DTTMSZ-1] = 0;
    cmsg->tmrange=iaccm;

    /*
     *    Convert Parameter information
     */
    gb2_param( tbls[0], tbls[1], cmsg, gem->parm, &scal, &missng, &ier );
    if ( ier != 0 ) {
        ER_WMSG("GB", &ier, "Couldn't get parameter values", &itmp, 2, 1);
        *iret=-34;
        return;
    }
    gem->iuscal=scal;
    gem->rmsval=missng;

    /*
     *    Convert Level information
     */
    gem->level[0]=-1;
    gem->level[1]=-1;
    gem->vcord=0;
    /*gb2_vcrd( tbls[2], tbls[3], cmsg, gem->level, &gem->vcord, &ier);*/
    /* S. Chiswell modification to get unit of vertical coordinate */
    gem->unit[0] = '\0';
    gb2_vcrd( tbls[2], tbls[3], cmsg, gem->level, &gem->vcord, gem->unit, &ier);
    if ( ier != 0 ) {
        ER_WMSG("GB", &ier, "Couldn't compute vertical co-ordinate values",
                &itmp, 2, 1);
        *iret=-34;
        return;
    }

    /*
     *    Convert GDS info to Gempak navigation block.
     */
#if 0
    gb2_gdsnav( tg, gem->cproj, &cmsg->kx, &cmsg->ky, gem->gdsarr,
                gem->corners, gem->navblk, &cmsg->g2scan_mode, &ier );
    if ( ier != 0 ) {
        er_wmsg("GB", &ier, " ", &itmp, 2, 1);
        *iret=-34;
        return;
    }
#endif
     /*printf("SAGgrd %d %s %d %d\n",*iret, gem->cproj,cmsg->kx,cmsg->ky);*/

}
