# LV2 build of VialFX. Reuses the shared code library and object directory of the Projucer-generated Makefile
# next to it, adds the LV2 wrapper bundled with the JUCE copy in third_party, links the plugin and writes the
# bundle (VialFX.lv2 with the .so and the .ttl files). Usage, after the normal build has made VialFX.a:
#   make -f lv2.mk CONFIG=Release AR=gcc-ar lv2
include Makefile

LV2_URI := urn:vialfx:vialfx
LV2_DEFINES := "-DJucePlugin_Build_LV2=1" "-DJucePlugin_LV2URI=\"$(LV2_URI)\"" "-DJucePlugin_LV2Category=\"Plugin\"" \
  "-DJucePlugin_WantsLV2Presets=1" "-DJucePlugin_WantsLV2State=1" "-DJucePlugin_WantsLV2TimePos=1" \
  "-DJucePlugin_MaxNumInputChannels=2"
JUCE_CPPFLAGS_LV2 := "-DJucePlugin_Build_VST=0" "-DJucePlugin_Build_VST3=0" "-DJucePlugin_Build_AU=0" "-DJucePlugin_Build_AUv3=0" \
  "-DJucePlugin_Build_RTAS=0" "-DJucePlugin_Build_AAX=0" "-DJucePlugin_Build_Standalone=0" "-DJucePlugin_Build_Unity=0" $(LV2_DEFINES)
LV2_BUNDLE := $(JUCE_OUTDIR)/VialFX.lv2
LV2_SO := $(JUCE_OUTDIR)/VialFX_lv2.so

.PHONY: lv2
lv2: $(LV2_BUNDLE)/manifest.ttl

$(JUCE_OBJDIR)/juce_LV2_Wrapper.o: ../../../third_party/JUCE/modules/juce_audio_plugin_client/LV2/juce_LV2_Wrapper.cpp
	-$(V_AT)mkdir -p $(JUCE_OBJDIR)
	@echo "Compiling juce_LV2_Wrapper.cpp"
	$(V_AT)$(CXX) $(JUCE_CXXFLAGS) $(JUCE_CPPFLAGS_LV2) $(JUCE_CFLAGS_VST) -o "$@" -c "$<"

$(LV2_SO): $(JUCE_OBJDIR)/juce_LV2_Wrapper.o $(JUCE_OUTDIR)/$(JUCE_TARGET_SHARED_CODE)
	@echo Linking "VialFX - LV2"
	$(V_AT)$(CXX) -o $@ $(JUCE_OBJDIR)/juce_LV2_Wrapper.o $(JUCE_OUTDIR)/$(JUCE_TARGET_SHARED_CODE) $(JUCE_LDFLAGS) $(JUCE_LDFLAGS_VST) $(TARGET_ARCH)

$(JUCE_OUTDIR)/lv2_ttl_generator: ../linux_lv2/lv2_ttl_generator.c
	$(CC) -o $@ $< -ldl

$(LV2_BUNDLE)/manifest.ttl: $(LV2_SO) $(JUCE_OUTDIR)/lv2_ttl_generator
	rm -rf $(LV2_BUNDLE)
	mkdir -p $(LV2_BUNDLE)
	cp $(LV2_SO) $(LV2_BUNDLE)/VialFX.so
	cd $(LV2_BUNDLE) && ../lv2_ttl_generator ./VialFX.so
	test -f $(LV2_BUNDLE)/manifest.ttl
