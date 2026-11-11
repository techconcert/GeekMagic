#include "cfgstore.h"

int cfg_load(const CfgBackend* b, GqConf* out) {
  GqConf t;
  if (b && b->read && b->read(b->ctx, (uint8_t*)&t, sizeof(t)) && gqc_check(&t) == 0) {
    gqc_sanitize(&t);
    *out = t;
    return 1;
  }
  gqc_defaults(out);
  return 0;
}

int cfg_save(const CfgBackend* b, GqConf* c) {
  if (!b || !b->write || !b->read) return 0;
  GqConf t = *c, back;
  gqc_sanitize(&t);
  if (!b->write(b->ctx, (const uint8_t*)&t, sizeof(t))) return 0;
  if (!b->read(b->ctx, (uint8_t*)&back, sizeof(back))) return 0;
  const uint8_t* x = (const uint8_t*)&t;
  const uint8_t* y = (const uint8_t*)&back;
  for (uint32_t i = 0; i < sizeof(t); i++)
    if (x[i] != y[i]) return 0;
  *c = t;
  return 1;
}
