/*	$NetBSD: ppm_test.c,v 1.3 2025/09/05 21:16:18 christos Exp $	*/

#include <stdio.h>
#include <stdlib.h>
#include "ppm.h"

int main(int argc, char *argv[])
{
  /*
   * argv[1]: user
   * argv[2]: password
   * argv[3]: configuration file
   */

  int ret = 1;

  if(argc > 2)
  {
    printf("Testing user %s password: '%s' against %s policy config file \n",
            argv[1], argv[2], argv[3]
          );

    /* format user entry */
#if OLDAP_VERSION == 0x0205
    char *errmsg = NULL;
#else
    char errbuf[256];
    struct berval errmsg = { sizeof(errbuf)-1, errbuf };
#endif
    Entry pEntry;
    pEntry.e_nname.bv_val=argv[1];
    pEntry.e_name.bv_val=argv[1];

    /* get configuration file content */
    struct berval pArg;
    FILE *fp;
    if ((fp = fopen(argv[3],"r")) == NULL)
    {
      fprintf(stderr,"Unable to open config file for reading\n");
      return ret;
    }
    char *fcontent = NULL;
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    fcontent = malloc(fsize);
    fread(fcontent, 1, fsize, fp);
    fclose(fp);
    pArg.bv_val = fcontent;
  
    ppm_test=1; // enable ppm_test for informing ppm not to use syslog

    ret = check_password(argv[2], &errmsg, &pEntry, &pArg);

    if(ret == 0)
    {
      printf("Password is OK!\n");
    }
    else
    {
#if OLDAP_VERSION == 0x0205
      printf("Password failed checks : %s\n", errmsg);
#else
      printf("Password failed checks : %s\n", errmsg.bv_val);
#endif
    }

#if OLDAP_VERSION == 0x0205
    ber_memfree(errmsg);
#else
    if (errmsg.bv_val != errbuf)
        ber_memfree(errmsg.bv_val);
#endif
    return ret;

  }

  return ret;
}



