# Data Structures & Algorithms Project (2024)

This was my implementation of the [2024 problem](./2023_2024.pdf) for the Data Structures and Algorithms course (B.Sc. Computer Science & Engineering @PoliMi, Milan, IT, 2024) which got the highest grade (30L/30L), thanks to a use of memory- and compute time complexity & constant scale factor-saving tricks, and the following data structures:

* Hash table with chaining for storage of recipes
* Dynamically allocated strings everywhere
* Circular linked lists for recipe ingredients updated with "last failed on" ingredient pointers, to reduce fail time for repeated attempts to make recipes that are not ready to be made yet
* Bitfields & packed structs for increased memory savings (bitwise accesses fully compatible with x86\_64 and arm64)
* [Trie](https://en.wikipedia.org/wiki/Trie)\* for ingredients in pantry

Unfortunately, an initial tentative use of tries with a rudimentary custom memory allocator did not satisfy memory constraints when employed in the HT's place, but this structure was left in for storage of ingredients because the dataset is small enough that it doesn't really matter, and also because *It's Cute*.

The repo is complete with a Python wrapper for the binary test-case generation and reference correct implementation binaries, and a "prettifier" for generated or provided test cases, to make testing and debugging easier.
