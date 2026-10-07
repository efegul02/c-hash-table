#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define INITIAL_BUCKETS 8
#define FILE_MAGIC      0x48544231u   /* "HTB1" - dosya tanima imzasi */
#define FILE_VERSION    1u
#define MAX_KEY_LEN     4096u         /* bozuk dosyalara karsi ust sinir */

/* ---------- Veri yapilari ---------- */

typedef struct Node {
    char        *key;      /* heap'te kopyalanmis anahtar */
    int32_t      value;
    struct Node *next;     /* ayni bucket'taki sonraki dugum (chaining) */
} Node;

typedef struct {
    Node  **buckets;       /* Node* dizisi */
    size_t  bucket_count;
    size_t  size;          /* toplam eleman sayisi */
} HashTable;

/* ---------- Hash fonksiyonu (djb2) ---------- */

static size_t hash_str(const char *s)
{
    size_t h = 5381;
    int c;
    while ((c = (unsigned char)*s++) != 0)
        h = ((h << 5) + h) + (size_t)c;   /* h * 33 + c */
    return h;
}

/* ---------- Temel islemler ---------- */

HashTable *ht_create(size_t bucket_count)
{
    HashTable *ht = malloc(sizeof *ht);
    if (!ht) return NULL;
    ht->buckets = calloc(bucket_count, sizeof *ht->buckets);  /* hepsi NULL */
    if (!ht->buckets) { free(ht); return NULL; }
    ht->bucket_count = bucket_count;
    ht->size = 0;
    return ht;
}

/* Anahtar varsa degeri gunceller, yoksa bucket'in basina ekler. */
int ht_put(HashTable *ht, const char *key, int32_t value)
{
    size_t idx = hash_str(key) % ht->bucket_count;

    for (Node *n = ht->buckets[idx]; n; n = n->next) {
        if (strcmp(n->key, key) == 0) { n->value = value; return 0; }
    }

    Node *node = malloc(sizeof *node);
    if (!node) return -1;
    node->key = malloc(strlen(key) + 1);
    if (!node->key) { free(node); return -1; }
    strcpy(node->key, key);
    node->value = value;

    node->next = ht->buckets[idx];     /* basa ekle: O(1) */
    ht->buckets[idx] = node;
    ht->size++;
    return 0;
}

/* Bulursa 1 doner ve *out'a yazar, bulamazsa 0 doner. */
int ht_get(const HashTable *ht, const char *key, int32_t *out)
{
    size_t idx = hash_str(key) % ht->bucket_count;
    for (Node *n = ht->buckets[idx]; n; n = n->next) {
        if (strcmp(n->key, key) == 0) { *out = n->value; return 1; }
    }
    return 0;
}

int ht_remove(HashTable *ht, const char *key)
{
    size_t idx = hash_str(key) % ht->bucket_count;
    Node *prev = NULL;
    for (Node *n = ht->buckets[idx]; n; prev = n, n = n->next) {
        if (strcmp(n->key, key) == 0) {
            if (prev) prev->next = n->next;
            else      ht->buckets[idx] = n->next;
            free(n->key);
            free(n);
            ht->size--;
            return 1;
        }
    }
    return 0;
}

void ht_free(HashTable *ht)
{
    if (!ht) return;
    for (size_t i = 0; i < ht->bucket_count; i++) {
        Node *n = ht->buckets[i];
        while (n) {
            Node *next = n->next;
            free(n->key);
            free(n);
            n = next;
        }
    }
    free(ht->buckets);
    free(ht);
}

/* ---------- Diske binary yazma ----------
 *
 * Dosya formati:
 *   [uint32 magic][uint32 version][uint64 bucket_count][uint64 size]
 *   sonra 'size' adet kayit:
 *   [uint32 key_len][key_len bayt (null YOK)][int32 value]
 */
int ht_save(const HashTable *ht, const char *path)
{
    FILE *f = fopen(path, "wb");                       /* 1. binary modda ac */
    if (!f) { perror("fopen"); return -1; }

    uint32_t magic   = FILE_MAGIC;                     /* 2. header yaz */
    uint32_t version = FILE_VERSION;
    uint64_t bcount  = (uint64_t)ht->bucket_count;
    uint64_t count   = (uint64_t)ht->size;

    if (fwrite(&magic,   sizeof magic,   1, f) != 1 ||
        fwrite(&version, sizeof version, 1, f) != 1 ||
        fwrite(&bcount,  sizeof bcount,  1, f) != 1 ||
        fwrite(&count,   sizeof count,   1, f) != 1)
        goto fail;

    for (size_t i = 0; i < ht->bucket_count; i++) {    /* 3. tum zincirleri gez */
        for (const Node *n = ht->buckets[i]; n; n = n->next) {
            uint32_t klen = (uint32_t)strlen(n->key);
            int32_t  val  = n->value;

            if (fwrite(&klen, sizeof klen, 1, f) != 1) goto fail;     /* uzunluk */
            if (fwrite(n->key, 1, klen, f) != klen)    goto fail;     /* anahtar */
            if (fwrite(&val, sizeof val, 1, f) != 1)   goto fail;     /* deger */
        }
    }

    if (fclose(f) != 0) { perror("fclose"); return -1; }              /* 4. kapat */
    return 0;

fail:
    perror("fwrite");
    fclose(f);
    return -1;
}

/* Dogrulama icin: dosyadan tabloyu geri yukler. */
HashTable *ht_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return NULL; }

    uint32_t magic, version;
    uint64_t bcount, count;
    if (fread(&magic,   sizeof magic,   1, f) != 1 ||
        fread(&version, sizeof version, 1, f) != 1 ||
        fread(&bcount,  sizeof bcount,  1, f) != 1 ||
        fread(&count,   sizeof count,   1, f) != 1 ||
        magic != FILE_MAGIC || version != FILE_VERSION || bcount == 0) {
        fprintf(stderr, "Gecersiz dosya basligi\n");
        fclose(f);
        return NULL;
    }

    HashTable *ht = ht_create((size_t)bcount);
    if (!ht) { fclose(f); return NULL; }

    for (uint64_t i = 0; i < count; i++) {
        uint32_t klen;
        int32_t  val;
        if (fread(&klen, sizeof klen, 1, f) != 1 || klen > MAX_KEY_LEN) goto fail;

        char *key = malloc((size_t)klen + 1);
        if (!key) goto fail;
        if (fread(key, 1, klen, f) != klen || fread(&val, sizeof val, 1, f) != 1) {
            free(key);
            goto fail;
        }
        key[klen] = '\0';                              /* null'u biz ekliyoruz */

        int rc = ht_put(ht, key, val);
        free(key);
        if (rc != 0) goto fail;
    }

    fclose(f);
    return ht;

fail:
    fprintf(stderr, "Dosya bozuk veya eksik\n");
    ht_free(ht);
    fclose(f);
    return NULL;
}

/* ---------- Deneme ---------- */

int main(void)
{
    HashTable *ht = ht_create(INITIAL_BUCKETS);
    ht_put(ht, "elma", 10);
    ht_put(ht, "armut", 20);
    ht_put(ht, "kiraz", 30);
    ht_put(ht, "elma", 99);               /* guncelleme */

    if (ht_save(ht, "tablo.bin") != 0) return 1;

    HashTable *yuklenen = ht_load("tablo.bin");
    if (!yuklenen) return 1;

    int32_t v;
    if (ht_get(yuklenen, "elma", &v)) printf("elma = %d\n", v);
    if (ht_get(yuklenen, "kiraz", &v)) printf("kiraz = %d\n", v);
    printf("eleman sayisi: %zu\n", yuklenen->size);

    ht_free(ht);
    ht_free(yuklenen);
    return 0;
}
