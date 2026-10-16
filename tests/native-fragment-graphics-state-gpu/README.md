This adds one focused actual-driver case to the existing owned cold-factory
fixture. It queries and enables supported `VK_EXT_extended_dynamic_state3`
features, creates the original pinned VS/PS pipeline with the same mutable
owned creation input later captured by production code, retires the original
objects, and verifies the actual factory's driver call after the source dynamic
array is overwritten. The original fixture's shader-pin, explicit feature,
validation, allocation, and release checks remain in force.

The required dynamic fields are rasterization samples, sample mask, and alpha to
coverage; queried alpha to one and sample-location enable are included when
supported. A missing required feature refuses the case rather than pretending
that unsupported state was tested. Use `run.py` with the same explicit private
`--original`, `--vertex`, `--record`, and `--driver` options as the original
owned-factory runner. Keep generated binaries, modules, and validation packages
private, and remove caches after reaping the run.

This proves real host-driver creation acceptance and owned pointer reconstruction
for these inputs. It records no command buffer and renders no pixels; it proves
no native receiver compatibility, source frame, eye, live mask, or passthrough.
