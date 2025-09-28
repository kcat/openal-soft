# The OpenAL Registry

This contains the OpenAL registry. Today, it only contains an XML registry limited to OpenAL Soft only. However, in the
future there are aspirations for this to function much like the Vulkan/OpenCL registry (i.e. manpages and specifications
generated from the contents of this registry) and cover implementations other than OpenAL Soft. The OpenGL registry
heavily inspired the XML schema used.

> [!NOTE]
> A [community effort](https://github.com/Raulshc/OpenAL-EXT-Repository/tree/master/xml) has made good progress at
> defining this, and it is possible that this can be used as a base for future work completing the OpenAL Registry.
> The current contents of the OpenAL Registry were made from scratch to ensure that the existing OpenAL Soft headers
> could be generated from the XML without material changes to the previously-handwritten headers. In addition, there are
> some [pending license questions](https://github.com/Raulshc/OpenAL-EXT-Repository/issues/6) that should be resolved
> before incorporating its contents into this repository.

The following OpenAL Soft headers are generated from the OpenAL Registry:
- al.h
- alc.h
- alext.h
- efx.h

The following OpenAL Soft headers are not generated from the OpenAL Registry:
- **efx-creative.h** - This is only a stub
- **efx-presets.h** - This is considered non-normative. The `struct` defined in this header is not referenced as part of
  the OpenAL API. It can be used to aid the loading of effects (as it is in the examples in the OpenAL Soft repository),
  and is (as of writing) used as an implementation detail, but this does not make it a core part of the OpenAL API as it
  currently stands.
