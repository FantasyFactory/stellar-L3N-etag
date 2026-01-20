#include <string.h>
#include "smadonnator.h"

/* Semplice PRNG Xorshift32 "tipo rand_custom()" */
/* Stato iniziale (seed); non deve essere 0 */
static unsigned long rng_state = 1463534242UL;

/* Permette di impostare il seed dall'esterno */
extern void srand_custom(unsigned long seed)
{
    if (seed != 0)
        rng_state = seed;
}

/* Generatore base: restituisce 32 bit pseudocasuali */
static unsigned long xorshift32(void)
{
    unsigned long x = rng_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    rng_state = x;
    return x;
}

/* Versione tipo rand_custom(): restituisce un intero tra 0 e max-1 */
int rand_custom(int max)
{
    /* Se vuoi esattamente 0..RAND_MAX stile C, sostituisci max con (1U<<31) */
    unsigned long r = xorshift32();
    return (int)(r % (unsigned long)max);
}
  
char *aggiungi_stringa(char *buff, const char *stringa) {

    size_t len = strlen(stringa);                 // lunghezza della stringa scelta
    memcpy(buff, stringa, len);                   // copia senza '\0' [web:36][web:45]
    buff += len;                            // avanza di len

    *buff = ' ';                            // aggiungi spazio
    buff++;                                 // avanza ancora

    *buff = '\0';                           // opzionale, se vuoi buffer sempre terminato
    return buff;
}

extern void smadonnator(char *buff)
    {
    int n,s1,d1,s2,d2;
    char *p=buff;
    size_t len;
    //srand_custom(clock_time());
    
    if (rand_custom(2)==0) {
        s1=(rand_custom(soggetti_size));
        d1=(rand_custom(aggettivi_size));
        p=aggiungi_stringa(p, soggetti[s1]);
        p=aggiungi_stringa(p, aggettivi[d1]);

        n=rand_custom(3);
        if (n!=0) {
            s1=(rand_custom(sorti_size));
            d1=(rand_custom(da_size));
            p=aggiungi_stringa(p, sorti[s1]);
            p=aggiungi_stringa(p, da[d1]);
            if (n==2) {
                *p++ = 'e';
                *p++ = ' ';
                do { s2=(rand_custom(sorti_size)); } while (s2==s1);
                do { d2=(rand_custom(da_size)); } while (d2==d1);
                p=aggiungi_stringa(p, sorti[s2]);
                p=aggiungi_stringa(p, da[d2]);            
            }
        }


    } else {
        memcpy(buff, "La madonna ", 11);
        p+=11;
        p=aggiungi_stringa(p, aggettivi_madonna[rand_custom(aggettivi_madonna_size)]);

        n=(rand_custom(3));
        if (n!=0) {
            s1=(rand_custom(sorti_madonna_size));
            d1=(rand_custom(da_size));
            p=aggiungi_stringa(p, sorti_madonna[s1]);
            p=aggiungi_stringa(p, da[d1]);
            if (n==2) {
                *p++ = 'e';
                *p++ = ' ';
                do { s2=(rand_custom(sorti_madonna_size)); } while (s2==s1);
                do { d2=(rand_custom(da_size)); } while (d2==d1);
                p=aggiungi_stringa(p, sorti_madonna[s2]);
                p=aggiungi_stringa(p, da[d2]);            
            }
        }
    }
}