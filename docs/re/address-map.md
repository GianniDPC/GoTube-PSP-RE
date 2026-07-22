# Address map

The PBP header is 40 bytes: magic, version, then eight little-endian segment
offsets. The boundaries below are mechanically parsed from the authoritative
EBOOT rather than copied from earlier notes.

| Segment | PBP start | PBP end (exclusive) | Size |
|---|---:|---:|---:|
| PARAM.SFO | `0x00000028` | `0x00000180` | 344 |
| ICON0.PNG | `0x00000180` | `0x000045c8` | 17,480 |
| ICON1.PMF | `0x000045c8` | `0x000045c8` | 0 |
| PIC0.PNG | `0x000045c8` | `0x000045c8` | 0 |
| PIC1.PNG | `0x000045c8` | `0x000045c8` | 0 |
| SND0.AT3 | `0x000045c8` | `0x000045c8` | 0 |
| DATA.PSP | `0x0001f664` | `0x0074f3c2` | 7,536,990 |
| DATA.PSAR | `0x0074f3c2` | `0x0074f3c2` | 0 |

For any byte in DATA.PSP, before ELF section/segment translation:

`PBP offset = DATA.PSP file offset + 0x1f664`

ELF file-offset to runtime-VA mappings are recorded only after parsing the ELF
program headers; a DATA.PSP offset must not be treated as a VA.

