# c-hash-table

C dilinde yazılmış, çarpışmaları (collision) **chaining** yöntemiyle çözen basit bir hash table. Tabloyu diske **binary** formatta kaydetme ve geri yükleme fonksiyonları da içerir.

## Özellikler

- Anahtar: `char *` (string), değer: `int32_t`
- Hash fonksiyonu: djb2
- Çarpışma çözümü: Her bucket bir bağlı liste (separate chaining)
- İşlemler: `ht_put` (ekle/güncelle), `ht_get`, `ht_remove`, `ht_free`
- Diske yazma/okuma: `ht_save`, `ht_load`

## Derleme ve çalıştırma

```bash
gcc -Wall -Wextra -std=c99 -o hashtable hashtable.c
./hashtable
```

Beklenen çıktı:

```
elma = 99
kiraz = 30
eleman sayisi: 3
```

Program çalışınca bulunduğu klasörde `tablo.bin` dosyası oluşur.

## Binary dosya formatı

```
[magic 4B][version 4B][bucket_count 8B][size 8B]
sonra `size` adet kayıt:
[key_len 4B][key baytları (null karakter yok)][value 4B]
```

- `magic` (`0x48544231`, "HTB1"): dosyanın bu formata ait olduğunu doğrular.
- `version`: format değişirse eski dosyaları ayırt etmek için.
- Pointer'lar değil, **veri** yazılır. Yüklerken her kayıt `ht_put` ile tabloya yeniden eklenir ve zincirler yeniden oluşur.
- Baytlar makinenin kendi endianness'ı ile yazılır, farklı mimariler arasında taşınabilirlik hedeflenmemiştir.

## Bilinen sınırlar

- Otomatik yeniden boyutlandırma (resize/rehash) yoktur, bucket sayısı sabittir.
- Değer tipi yalnızca `int32_t`'dir.

## Fikirler (yapılabilecekler)

- Load factor aşılınca otomatik resize
- Değer olarak `void *` veya `struct` desteği
- Endianness'tan bağımsız dosya formatı
