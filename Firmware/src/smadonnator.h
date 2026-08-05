#pragma once

char *da[]={
        "dal dio bubu","dallo scanato gesu boia",
        "dalla madonna maiala","dalla portanna la madonna",
        "dalla canaja de dio","dai fedeli inferociti",
        "dai testimoni di geova","dai terrapiattisti",
        "dagli spacciatori di ostie","dai musulmani"
    };

const int da_size = sizeof(da) / sizeof(da[0]);

const char *aggettivi_madonna[]={    
        "maiala","puttana","puttina","rincoglionita",
        "stitica","putrefatta","con le croste al culo",
        "col covid","pompinara","lesbica brutta", 
        "televenditrice", "schiava del sesso", "sporca",
        "malata di sifilide", "che si masturba con un cetriolo", "malvagia"
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
        "imbroglione", "immondo", "inutile", "infame",
        "pezzente", "schifoso", "maligno"
    };
static int aggettivi_size = sizeof(aggettivi) / sizeof(aggettivi[0]);

const char *sorti_madonna[]={
        "squoiata","inculata a sangue","presa a bastonate",
        "frustata","presa a calci","sodomizzata",
        "costretta a prostituirsi","che insulta suo figlio incitata",
        "calpestata dai fedeli", "su Onlyfans", "mangiata dai topi",
        "che si accende una canna in una camera iperbarica",
        "beccata a rubare i soldi delle offerte"
    };
static int sorti_madonna_size = sizeof(sorti_madonna) / sizeof(sorti_madonna[0]);

const char *sorti[]={
        "frustato in piazza","sodomizzato","affogato nella merda",
        "avvelenato","picchiato a sangue","coperto di insulti",
        "inculato con l'albero di natale","inculato col crocifisso",
        "messo nel presepe al posto dell'asinello",
        "venduto su temu","costretto a sputare controvento",
        "talmente stupido da essere deriso", "contromano in tangenziale",
        "che si da fuoco come un bonzo", "che sfila al gay pride"
    };
static int sorti_size = sizeof(sorti) / sizeof(sorti[0]);

const char *soggetti[]={
        "Il Dio porco",
        "Porco di quel Dio",
        "Dio",
        "Il Dio stronzo",
        "Gesu",
        "Gesu merda",
        "Cristo",
        "Il Cristo bastardo",
        "San Giuseppe",
        "Il papa",
        "La merda del clero",
        "Padre Pio"
    };
static int soggetti_size = sizeof(soggetti) / sizeof(soggetti[0]);

char *aggiungi_stringa(char *buff, const char *stringa);
extern void smadonnator(char *buff);
void srand_custom(unsigned long seed);
static unsigned long xorshift32(void);
int rand_custom(int max);