# Memory Map Assumptions

- The kernel is linked as a higher-half kernel (`-mcmodel=kernel`).
- Limine provides the memory map and HHDM offset.
- Physical memory map entries are read from Limine's memmap request.
- The kernel tracks usable and reserved pages from the Limine memmap.

If paging or mappings change, update this document with the new assumptions.
