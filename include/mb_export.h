#ifndef MB_EXPORT_H
#define MB_EXPORT_H

/* Embedded exports are usually not needed. */
#ifndef MB_EXPORT
#define MB_EXPORT
#endif

/* BIT (0) / BIT(0) */
#ifndef BIT
#define BIT(x) (1u << (x))
#endif

/* Minimal assert */
#ifndef CC_ASSERT
#define CC_ASSERT(cond) do { if (!(cond)) { for(;;) {} } } while (0)
#endif

#endif /* MB_EXPORT_H */
