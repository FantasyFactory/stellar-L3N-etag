#pragma once

char *da[]={
        "dalla madonna maiala","dalla portanna la madonna",
        "dal dio bubù","dallo scanato gesù boia",
        "dalla canaja de dio","dai fedeli inferociti",
        "dai testimoni di geova","dai terrapiattisti",
        "dagli spacciatori di ostie","dai musulmani"
    };

const int da_size = sizeof(da) / sizeof(da[0]);

const char *aggettivi_madonna[]={    
        "maiala","puttana","puttina","rincoglionita",
        "stitica","putrefatta","con le croste al culo",
        "col covid","pompinara","lesbica brutta"
    };
static int aggettivi_madonna_size = sizeof(aggettivi_madonna) / sizeof(aggettivi_madonna[0]);
    
const char *aggettivi[]={    
        "porco","cane","maiale","merdoso","putrido",
        "succhiatore di cazzi","col covid","pedofilo",
        "torturatore di animali","spacciatore di droga",
        "ladro di pollame","licantropo","onanista",
        "parassita","ciclista senza sellino","evasore fiscale",
        "impestato","idiota","bestemmiatore per primo",
        "ausiliario del traffico","scopino del cesso",
        "comunista","cassonetto dell'umido",
        "al posto della carta igienica","miserabile","serpentiforme",
        "iperdiarroico", "coprofilo", "necrofilo", 
    };
static int aggettivi_size = sizeof(aggettivi) / sizeof(aggettivi[0]);

const char *sorti_madonna[]={
        "squoiata","inculata a sangue","presa a bastonate",
        "frustata","presa a calci","sodomizzata",
        "costretta a prostituirsi","che insulta suo figlio incitata"
    };
static int sorti_madonna_size = sizeof(sorti_madonna) / sizeof(sorti_madonna[0]);

const char *sorti[]={
        "frustato in piazza","sodomizzato","affogato nella merda",
        "avvelenato","picchiato a sangue","coperto di insulti",
        "inculato con l'albero di natale","inculato col crocifisso",
        "messo nel presepe al posto dell'asinello",
        "venduto su temu","costretto a sputare controvento",
        "talmente stupido da essere deriso"
    };
static int sorti_size = sizeof(sorti) / sizeof(sorti[0]);

const char *soggetti[]={
        "Il dio porco",
        "Porco di quel dio",
        "Dio",
        "Il dio stronzo",
        "Gesù",
        "Gesù merda",
        "San giuseppe",
        "Il papa",
        "La merda del clero",
        "Padre pio"
    };
static int soggetti_size = sizeof(soggetti) / sizeof(soggetti[0]);

char *aggiungi_stringa(char *buff, const char *stringa);
extern void smadonnator(char *buff);
void srand_custom(unsigned long seed);
static unsigned long xorshift32(void);
int rand_custom(int max);