/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sound_engine.h"

#include "fx_tables.h"

#include "chorus_module.h"
#include "compressor_module.h"
#include "flanger_module.h"
#include "phaser_module.h"
#include "decimator.h"
#include "effects_input_stage.h"
#include "wave_warp.h"
#include "spectral_morph_processor.h"
#include "filters_module.h"
#include "upsampler.h"
#include "modulation_connection_processor.h"
#include "synth_constants.h"
#include "effects_modulation_handler.h"
#include "peak_meter.h"
#include "operators.h"
#include "fx_effect_chain.h"
#include "other_filter.h"
#include "value_switch.h"

namespace vital {

  SoundEngine::SoundEngine() : SynthModule(0, 1), modulation_handler_(nullptr), audio_input_(nullptr),
                               sidechain_input_(nullptr), sidechain_in_(nullptr), silence_(),
                               filter_router_(nullptr), filters_module_(nullptr), filters_bank_(nullptr), upsampler_(nullptr),
                               last_oversampling_amount_(-1), last_sample_rate_(-1), peak_meter_(nullptr) {
    for (int& curve : point_mod_curve_)
      curve = -1;
    // The four wavetables (input/sidechain warp table, input/sidechain spectral table) start as the built-in ones;
    // the editor can replace their contents. They exist before the graph, whose processors read them.
    for (int i = 0; i < kNumFxTables; ++i) {
      fx_tables_[i] = std::make_unique<Wavetable>(kNumOscillatorWaveFrames);
      FxTables::fillWavetable(fx_tables_[i].get(), FxTables::kindOfSlot(i));
    }
    // The drawn curves of the Custom model of the Other filters exist before the graph too: every filter finds them
    // through the table that is current while the graph is built.
    const LineGenerator* filter_curve_table[kNumFilterCurveSets * kNumSpectralCurves];
    for (int set = 0; set < kNumFilterCurveSets; ++set) {
      int lane = kFirstFilterLane + set;
      for (int i = 0; i < kNumSpectralCurves; ++i) {
        spectral_curves_[lane][i] = std::make_unique<LineGenerator>();
        filter_curve_table[set * kNumSpectralCurves + i] = spectral_curves_[lane][i].get();
      }
      resetSpectralDrawings(lane);
    }
    OtherFilter::CurveScope curve_scope(filter_curve_table);
    init();
    bps_ = data_->controls["beats_per_minute"];
    modulation_processors_.reserve(kMaxModulationConnections);
  }

  SoundEngine::~SoundEngine() {
    modulation_handler_->prepareDestroy();
  }

  void SoundEngine::init() {
    createBaseControl("bypass");

    // Interface-level controls the synth engine also registers. The GUI sets these by name, so they have to
    // exist (an unregistered name used to crash the plugin when its button or knob was touched).
    createBaseControl("mpe_enabled");
    createBaseControl("view_spectrogram");
    createBaseControl("voice_amplitude");  // voice section knob; there is no voice audio in an effect

    // Placeholder controls for the input panels; nothing reads them until spectral morph / distortion exist.
    Output* input_spectral_amount = createMonoModControl("input_spectral_morph_amount");
    Output* sidechain_spectral_amount = createMonoModControl("sidechain_spectral_morph_amount");
    Value* input_spectral_mode = createBaseControl("input_spectral_morph_type");
    Value* sidechain_spectral_mode = createBaseControl("sidechain_spectral_morph_type");
    Value* input_random_dice = createBaseControl("input_random_dice");
    Value* sidechain_random_dice = createBaseControl("sidechain_random_dice");
    Output* input_random_shuffle = createMonoModControl("input_random_shuffle");
    Output* sidechain_random_shuffle = createMonoModControl("sidechain_random_shuffle");
    Output* input_spectral_position = createMonoModControl("input_spectral_position");
    Output* sidechain_spectral_position = createMonoModControl("sidechain_spectral_position");
    Value* fft_size = createBaseControl("fft_size");
    fft_size_ = fft_size;
    input_spectral_mode_ = input_spectral_mode;
    sidechain_spectral_mode_ = sidechain_spectral_mode;
    spectral_engaged_ = new Value(0.0f);
    addIdleProcessor(spectral_engaged_);
    Value* input_destination = createBaseControl("input_destination");
    Value* sidechain_destination = createBaseControl("sidechain_destination");

    // Lane switches, levels and pans (both inputs). The levels and pans can be modulated.
    Value* input_on = createBaseControl("input_on");
    Output* input_level = createMonoModControl("input_level");
    Output* input_pan = createMonoModControl("input_pan");
    Value* sidechain_on = createBaseControl("sidechain_on");
    // Distortion warp: mode and (modulatable) amount for each lane.
    Value* input_warp_mode = createBaseControl("input_distortion_type");
    Output* input_warp_amount = createMonoModControl("input_distortion_amount");
    Value* sidechain_warp_mode = createBaseControl("sidechain_distortion_type");
    Output* sidechain_warp_amount = createMonoModControl("sidechain_distortion_amount");
    Output* sidechain_level = createMonoModControl("sidechain_level");
    Output* sidechain_pan = createMonoModControl("sidechain_pan");
    oversampling_ = createBaseControl("oversampling");
    legato_ = createBaseControl("legato");

    Output* stereo_routing = createMonoModControl("stereo_routing");
    Value* stereo_mode = createBaseControl("stereo_mode");
    Output* beats_per_second = createMonoModControl("beats_per_minute");
    cr::LowerBound* beats_per_second_clamped = new cr::LowerBound(0.0f);
    beats_per_second_clamped->plug(beats_per_second);
    addProcessor(beats_per_second_clamped);

    Output* polyphony = createMonoModControl("polyphony");
    Value* voice_priority = createBaseControl("voice_priority");
    Value* voice_override = createBaseControl("voice_override");

    modulation_handler_ = new EffectsModulationHandler(beats_per_second_clamped->output());
    addSubmodule(modulation_handler_);
    modulation_handler_->setPolyphony(vital::kMaxPolyphony);
    // The FX engine is monophonic: one permanent voice carries the modulators and every new note
    // steals it. The polyphony/voice_override parameters stay registered but are not used here.
    Value* fixed_polyphony = new Value(1.0f);
    Value* fixed_override = new Value(static_cast<mono_float>(VoiceHandler::kSteal));
    addIdleProcessor(fixed_polyphony);
    addIdleProcessor(fixed_override);
    modulation_handler_->plug(fixed_polyphony, VoiceHandler::kPolyphony);
    modulation_handler_->plug(voice_priority, VoiceHandler::kVoicePriority);
    modulation_handler_->plug(fixed_override, VoiceHandler::kVoiceOverride);

    addProcessor(modulation_handler_);

    // Same as the synth engine: these only expose the wheel parameters to the host.
    // Actual wheel values reach the modulation handler through setPitchWheel/setModWheel.
    createBaseControl("pitch_wheel");
    createBaseControl("mod_wheel");

    // Input audio is held here at the base sample rate and fed through the filters before the rack.
    audio_input_ = new Upsampler();
    addIdleProcessor(audio_input_);

    sidechain_input_ = new Upsampler();
    addIdleProcessor(sidechain_input_);

    AudioLane* main_lane = new AudioLane();
    main_lane->plug(audio_input_, AudioLane::kAudio);
    main_lane->plug(input_on, AudioLane::kOn);
    main_lane->plug(input_level, AudioLane::kLevel);
    main_lane->plug(input_pan, AudioLane::kPan);
    addProcessor(main_lane);

    // The sidechain lane's modulation path is independent of its level and pan (like an oscillator B whose
    // level is down: it still modulates). Only its on switch gates it. The level and pan are applied last,
    // to the signal that goes on to the filters / effects.
    Value* unity_level = new Value(1.0f);
    Value* centre_pan = new Value(0.0f);
    Value* always_on = new Value(1.0f);
    addIdleProcessor(unity_level);
    addIdleProcessor(centre_pan);
    addIdleProcessor(always_on);

    AudioLane* sidechain_gate = new AudioLane();
    sidechain_gate->plug(sidechain_input_, AudioLane::kAudio);
    sidechain_gate->plug(sidechain_on, AudioLane::kOn);
    sidechain_gate->plug(unity_level, AudioLane::kLevel);
    sidechain_gate->plug(centre_pan, AudioLane::kPan);
    addProcessor(sidechain_gate);

    // Each lane runs its spectral morph and its distortion warp in either order (the swap button between the two
    // dropdowns, <lane>_warp_first). Both orders exist as separate chains and a switch picks one. The chain that is
    // not selected has its modes forced to None, so it only passes audio through (the spectral stage's idle path is
    // a plain delay line) and costs next to nothing. Both orders delay by the same amount.
    struct LaneControls {
      Output *table_mix, *table_position, *table_gain, *table_resonance;
      Output *gain, *density, *resonance, *spread, *depth, *phase;
      Output *vocode_formant = nullptr, *vocode_flatten = nullptr, *vocode_noise = nullptr, *vocode_emphasis = nullptr,
             *vocode_gate = nullptr, *vocode_level = nullptr, *vocode_whiten = nullptr;
      Value *vocode_bands = nullptr, *vocode_attack = nullptr, *vocode_release = nullptr;
      Output *warp_phase, *warp_bias, *warp_smooth, *warp_drive, *warp_mix;
      Value* warp_dc;
    };
    auto makeControls = [this](const std::string& p, bool main) {
      LaneControls c;
      c.table_mix = createMonoModControl(p + "_spectral_table_mix");
      c.table_position = createMonoModControl(p + "_table_position");
      c.table_gain = createMonoModControl(p + "_table_gain");
      c.table_resonance = createMonoModControl(p + "_table_resonance");
      c.gain = createMonoModControl(p + "_spectral_gain");
      c.density = createMonoModControl(p + "_spectral_density");
      c.resonance = createMonoModControl(p + "_spectral_resonance");
      c.spread = createMonoModControl(p + "_spectral_spread");
      c.depth = createMonoModControl(p + "_spectral_depth");
      c.phase = createMonoModControl(p + "_spectral_phase");
      if (main) {
        c.vocode_bands = createBaseControl("input_vocoder_bands");
        c.vocode_attack = createBaseControl("input_vocoder_attack");
        c.vocode_release = createBaseControl("input_vocoder_release");
        c.vocode_formant = createMonoModControl("input_vocoder_formant");
        c.vocode_flatten = createMonoModControl("input_vocoder_flatten");
        c.vocode_noise = createMonoModControl("input_vocoder_noise");
        c.vocode_emphasis = createMonoModControl("input_vocoder_emphasis");
        c.vocode_gate = createMonoModControl("input_vocoder_gate");
        c.vocode_level = createMonoModControl("input_vocoder_level");
        c.vocode_whiten = createMonoModControl("input_vocoder_whiten");
      }
      c.warp_phase = createMonoModControl(p + "_distortion_phase");
      c.warp_bias = createMonoModControl(p + "_distortion_bias");
      c.warp_smooth = createMonoModControl(p + "_distortion_smooth");
      c.warp_drive = createMonoModControl(p + "_distortion_drive");
      c.warp_dc = createBaseControl(p + "_distortion_dc_filter");
      c.warp_mix = createMonoModControl(p + "_distortion_mix");
      return c;
    };
    auto gated = [this](Value* value, Value* flag, bool active_when_flag) -> Processor* {
      OrderGate* gate = new OrderGate(active_when_flag);
      gate->plug(value, OrderGate::kValue);
      gate->plug(flag, OrderGate::kFlag);
      addProcessor(gate);
      return gate;
    };
    auto makeSpectral = [this](bool main, const LaneControls& c, Processor* audio, Processor* mode,
                               Output* amount, Output* position, Output* shuffle, Value* dice, Processor* modulator,
                               int table_index) {
      SpectralMorphProcessor* sp = new SpectralMorphProcessor(main);
      sp->plug(audio, SpectralMorphProcessor::kAudio);
      sp->plug(mode, SpectralMorphProcessor::kMode);
      sp->plug(amount, SpectralMorphProcessor::kAmount);
      sp->plug(fft_size_, SpectralMorphProcessor::kFftSize);
      sp->plug(spectral_engaged_, SpectralMorphProcessor::kEngaged);
      sp->plug(c.table_mix, SpectralMorphProcessor::kTableMix);
      sp->plug(c.table_position, SpectralMorphProcessor::kTablePosition);
      sp->plug(c.table_gain, SpectralMorphProcessor::kTableGain);
      sp->plug(c.table_resonance, SpectralMorphProcessor::kTableResonance);
      sp->plug(position, SpectralMorphProcessor::kPosition);
      sp->plug(shuffle, SpectralMorphProcessor::kShuffle);
      sp->plug(dice, SpectralMorphProcessor::kDice);
      sp->plug(c.gain, SpectralMorphProcessor::kGain);
      sp->plug(c.density, SpectralMorphProcessor::kDensity);
      sp->plug(c.resonance, SpectralMorphProcessor::kResonance);
      sp->plug(c.spread, SpectralMorphProcessor::kSpread);
      sp->plug(c.depth, SpectralMorphProcessor::kDepth);
      sp->plug(c.phase, SpectralMorphProcessor::kPhase);
      if (main) {
        sp->plug(modulator, SpectralMorphProcessor::kModulator);
        sp->plug(c.vocode_bands, SpectralMorphProcessor::kVocodeBands);
        sp->plug(c.vocode_attack, SpectralMorphProcessor::kVocodeAttack);
        sp->plug(c.vocode_release, SpectralMorphProcessor::kVocodeRelease);
        sp->plug(c.vocode_formant, SpectralMorphProcessor::kVocodeFormant);
        sp->plug(c.vocode_flatten, SpectralMorphProcessor::kVocodeFlatten);
        sp->plug(c.vocode_noise, SpectralMorphProcessor::kVocodeNoise);
        sp->plug(c.vocode_emphasis, SpectralMorphProcessor::kVocodeEmphasis);
        sp->plug(c.vocode_gate, SpectralMorphProcessor::kVocodeGate);
        sp->plug(c.vocode_level, SpectralMorphProcessor::kVocodeLevel);
        sp->plug(c.vocode_whiten, SpectralMorphProcessor::kVocodeWhiten);
      }
      sp->setTable(fx_tables_[table_index].get());
      addProcessor(sp);
      return sp;
    };
    auto makeWarp = [this](bool main, const LaneControls& c, Processor* audio, Processor* mode, Output* amount,
                           int table_index) {
      WaveWarp* warp = new WaveWarp(main);
      warp->plug(audio, WaveWarp::kAudio);
      warp->plug(mode, WaveWarp::kMode);
      warp->plug(amount, WaveWarp::kAmount);
      warp->plug(c.warp_phase, WaveWarp::kPhase);
      warp->plug(c.warp_bias, WaveWarp::kBias);
      warp->plug(c.warp_smooth, WaveWarp::kSmooth);
      warp->plug(c.warp_drive, WaveWarp::kDrive);
      warp->plug(c.warp_dc, WaveWarp::kDcFilter);
      warp->plug(c.warp_mix, WaveWarp::kMix);
      warp->setTable(fx_tables_[table_index].get());
      addProcessor(warp);
      return warp;
    };
    auto makeSwitch = [this](Processor* spectral_first, Processor* warp_first, Value* flag) {
      OrderSwitch* order = new OrderSwitch();
      order->plug(spectral_first, OrderSwitch::kA);
      order->plug(warp_first, OrderSwitch::kB);
      order->plug(flag, OrderSwitch::kFlag);
      addProcessor(order);
      return order;
    };

    Value* input_warp_first = createBaseControl("input_warp_first");
    Value* sidechain_warp_first = createBaseControl("sidechain_warp_first");
    LaneControls main_controls = makeControls("input", true);
    LaneControls sidechain_controls = makeControls("sidechain", false);

    // The sidechain lane goes first; the main lane's FM and RM modes use its result as their modulator. The main
    // lane's modulator must line up with the main audio at the warp: spectral first, that audio is already delayed
    // by the spectral stage, so it gets the finished sidechain lane; warp first, it is not, so it gets the
    // sidechain's warp before any delay (that warp is never gated, so it is always there).
    SpectralMorphProcessor* sidechain_spectral = makeSpectral(
        false, sidechain_controls, sidechain_gate, gated(sidechain_spectral_mode, sidechain_warp_first, false),
        sidechain_spectral_amount, sidechain_spectral_position, sidechain_random_shuffle, sidechain_random_dice,
        nullptr, 3);
    WaveWarp* sidechain_warp = makeWarp(false, sidechain_controls, sidechain_spectral,
                                        gated(sidechain_warp_mode, sidechain_warp_first, false),
                                        sidechain_warp_amount, 1);
    WaveWarp* sidechain_warp_early = makeWarp(false, sidechain_controls, sidechain_gate, sidechain_warp_mode,
                                              sidechain_warp_amount, 1);
    SpectralMorphProcessor* sidechain_spectral_late = makeSpectral(
        false, sidechain_controls, sidechain_warp_early, gated(sidechain_spectral_mode, sidechain_warp_first, true),
        sidechain_spectral_amount, sidechain_spectral_position, sidechain_random_shuffle, sidechain_random_dice,
        nullptr, 3);
    OrderSwitch* sidechain_order = makeSwitch(sidechain_warp, sidechain_spectral_late, sidechain_warp_first);

    SpectralMorphProcessor* main_spectral = makeSpectral(
        true, main_controls, main_lane, gated(input_spectral_mode, input_warp_first, false),
        input_spectral_amount, input_spectral_position, input_random_shuffle, input_random_dice, sidechain_gate, 2);
    WaveWarp* main_warp = makeWarp(true, main_controls, main_spectral,
                                   gated(input_warp_mode, input_warp_first, false), input_warp_amount, 0);
    main_warp->plug(sidechain_order, WaveWarp::kModulator);
    main_warp->plug(sidechain_on, WaveWarp::kModulatorOn);
    WaveWarp* main_warp_early = makeWarp(true, main_controls, main_lane,
                                         gated(input_warp_mode, input_warp_first, true), input_warp_amount, 0);
    main_warp_early->plug(sidechain_warp_early, WaveWarp::kModulator);
    main_warp_early->plug(sidechain_on, WaveWarp::kModulatorOn);
    SpectralMorphProcessor* main_spectral_late = makeSpectral(
        true, main_controls, main_warp_early, gated(input_spectral_mode, input_warp_first, true),
        input_spectral_amount, input_spectral_position, input_random_shuffle, input_random_dice, sidechain_gate, 2);
    OrderSwitch* main_order = makeSwitch(main_warp, main_spectral_late, input_warp_first);

    // Drawn curves (Spectral Filter / Phaser / Flanger) and Random Amplitudes seeds, per lane (both chains share
    // the curves and keep the same seed).
    spectral_[0] = main_spectral;
    spectral_[1] = sidechain_spectral;
    spectral_alt_[0] = main_spectral_late;
    spectral_alt_[1] = sidechain_spectral_late;
    for (int lane = 0; lane < 2; ++lane) {
      for (int i = 0; i < kNumSpectralCurves; ++i) {
        spectral_curves_[lane][i] = std::make_unique<LineGenerator>();
        spectral_[lane]->setCurve(i, spectral_curves_[lane][i].get());
        spectral_alt_[lane]->setCurve(i, spectral_curves_[lane][i].get());
      }
      resetSpectralDrawings(lane);
    }

    AudioLane* sidechain_out = new AudioLane();
    sidechain_out->plug(sidechain_order, AudioLane::kAudio);
    sidechain_out->plug(always_on, AudioLane::kOn);
    sidechain_out->plug(sidechain_level, AudioLane::kLevel);
    sidechain_out->plug(sidechain_pan, AudioLane::kPan);
    addProcessor(sidechain_out);

    // The input panels' spectrum displays show each lane after its level, pan and warp.
    input_memory_ = std::make_shared<StereoMemory>(kAudioMemorySamples);
    sidechain_memory_ = std::make_shared<StereoMemory>(kAudioMemorySamples);
    AudioTap* main_tap = new AudioTap(input_memory_.get());
    main_tap->plug(main_order, AudioTap::kAudio);
    addProcessor(main_tap);
    AudioTap* sidechain_tap = new AudioTap(sidechain_memory_.get());
    sidechain_tap->plug(sidechain_out, AudioTap::kAudio);
    addProcessor(sidechain_tap);

    filter_router_ = new AudioFilterRouter();
    filter_router_->plug(main_tap, AudioFilterRouter::kAudio);
    filter_router_->plug(sidechain_tap, AudioFilterRouter::kSidechain);
    filter_router_->setDestinations(input_destination, sidechain_destination);
    addProcessor(filter_router_);

    filters_module_ = new FiltersModule();
    filters_module_->setMono(true);
    addSubmodule(filters_module_);
    addProcessor(filters_module_);
    filters_module_->plug(filter_router_->output(AudioFilterRouter::kToFilter1), FiltersModule::kFilter1Input);
    filters_module_->plug(filter_router_->output(AudioFilterRouter::kToFilter2), FiltersModule::kFilter2Input);
    // The key tracking / MIDI inputs are connected after SynthModule::init() below: the modulation handler
    // only creates its played-note outputs in its own init().

    // Filters 1 and 2 follow every held note: copies of the filters (own state, same settings) run for each note,
    // and the result is their average (see KeytrackBank).
    filters_bank_ = new KeytrackBank(filters_module_, false, kEngineKeytrackVoices, &keytrack_notes_,
                                     modulation_handler_->midi_offset_output(),
                                     [this](Output* keytrack, Output* midi) {
      FiltersModule* copy = new FiltersModule();
      copy->setMono(true);
      copy->plug(filter_router_->output(AudioFilterRouter::kToFilter1), FiltersModule::kFilter1Input);
      copy->plug(filter_router_->output(AudioFilterRouter::kToFilter2), FiltersModule::kFilter2Input);
      copy->plug(keytrack, FiltersModule::kKeytrack);
      copy->plug(midi, FiltersModule::kMidi);
      return copy;
    }, false);
    filters_bank_->plug(filters_module_, 0);
    filters_bank_->addGate("filter_1_keytrack", true);
    filters_bank_->addGate("filter_2_keytrack", true);
    addProcessor(filters_bank_);

    Add* filter_sum = new Add();
    filter_sum->plug(filters_bank_, 0);
    filter_sum->plug(filter_router_->output(AudioFilterRouter::kRawOut), 1);
    // The upsampler reads this at the oversampled block size, so make the buffer big enough for that.
    filter_sum->output()->ensureBufferSize(kMaxBufferSize * kMaxOversample);
    addProcessor(filter_sum);

    upsampler_ = new AudioUpsampler();
    upsampler_->plug(filter_sum, AudioUpsampler::kAudio);
    addProcessor(upsampler_);

    effect_chain_ = new FxEffectChain(beats_per_second, modulation_handler_->midi_offset_output(), &keytrack_notes_);
    addSubmodule(effect_chain_);
    addProcessor(effect_chain_);
    effect_chain_->plug(upsampler_, FxEffectChain::kAudio);

    // The effects' meters and displays read these; every copy of an effect has its own.
    for (int copy = 0; copy < kMaxEffectInstances; ++copy) {
      auto id = [copy](int type) { return FxEffectChain::instanceId(type, copy); };

      SynthModule* compressor = effect_chain_->getEffect(constants::kCompressor, copy);
      std::string compressor_id = id(constants::kCompressor);
      createStatusOutput(compressor_id + "_low_input", compressor->output(CompressorModule::kLowInputMeanSquared));
      createStatusOutput(compressor_id + "_band_input", compressor->output(CompressorModule::kBandInputMeanSquared));
      createStatusOutput(compressor_id + "_high_input", compressor->output(CompressorModule::kHighInputMeanSquared));
      createStatusOutput(compressor_id + "_low_output", compressor->output(CompressorModule::kLowOutputMeanSquared));
      createStatusOutput(compressor_id + "_band_output", compressor->output(CompressorModule::kBandOutputMeanSquared));
      createStatusOutput(compressor_id + "_high_output", compressor->output(CompressorModule::kHighOutputMeanSquared));

      SynthModule* chorus = effect_chain_->getEffect(constants::kChorus, copy);
      for (int i = 0; i < ChorusModule::kMaxDelayPairs; ++i)
        createStatusOutput(id(constants::kChorus) + "_delays" + std::to_string(i + 1), chorus->output(i + 1));

      SynthModule* phaser = effect_chain_->getEffect(constants::kPhaser, copy);
      createStatusOutput(id(constants::kPhaser) + "_cutoff", phaser->output(PhaserModule::kCutoffOutput));

      SynthModule* flanger = effect_chain_->getEffect(constants::kFlanger, copy);
      createStatusOutput(id(constants::kFlanger) + "_delay_frequency",
                         flanger->output(FlangerModule::kFrequencyOutput));
    }

    Decimator* decimator = new Decimator(3);
    decimator->plug(effect_chain_);
    addProcessor(decimator);

    // Inputs routed to "direct out" skip the filters and the effects and join the signal here.
    Add* with_direct = new Add();
    with_direct->plug(decimator, 0);
    with_direct->plug(filter_router_->output(AudioFilterRouter::kDirectOut), 1);
    addProcessor(with_direct);

    StereoEncoder* decoder = new StereoEncoder(true);
    decoder->plug(with_direct, StereoEncoder::kAudio);
    decoder->plug(stereo_routing, StereoEncoder::kEncodingValue);
    decoder->plug(stereo_mode, StereoEncoder::kMode);
    addProcessor(decoder);

    Output* volume = createMonoModControl("volume");
    SmoothVolume* scaled_audio = new SmoothVolume();
    scaled_audio->plug(decoder, SmoothVolume::kAudioRate);
    scaled_audio->plug(volume, SmoothVolume::kDb);

    peak_meter_ = new PeakMeter();
    peak_meter_->plug(scaled_audio, 0);
    createStatusOutput("peak_meter", peak_meter_->output());

    Clamp* clamp = new Clamp(-2.1f, 2.1f);
    clamp->plug(scaled_audio);
    
    addProcessor(peak_meter_);
    addProcessor(scaled_audio);

    addProcessor(clamp);
    clamp->useOutput(output());

    SynthModule::init();

    VITAL_ASSERT(modulation_handler_->midi_offset_output() != nullptr);
    VITAL_ASSERT(modulation_handler_->bent_midi_output() != nullptr);
    filters_module_->plug(modulation_handler_->midi_offset_output(), FiltersModule::kKeytrack);
    filters_module_->plug(modulation_handler_->bent_midi_output(), FiltersModule::kMidi);
    filters_bank_->setNoteOutputs(modulation_handler_->bent_midi_output(), modulation_handler_->current_midi_output());
    effect_chain_->connectMorph(fft_size_);

    // The Morph effects draw their curves and read their tables from the engine, like the lanes do.
    for (int instance = 0; instance < kMaxEffectInstances; ++instance) {
      int lane = morphLane(instance);
      MorphModule* morph = static_cast<MorphModule*>(effect_chain_->getEffect(constants::kMorph, instance));
      for (int i = 0; i < kNumSpectralCurves; ++i) {
        spectral_curves_[lane][i] = std::make_unique<LineGenerator>();
        morph->setCurve(i, spectral_curves_[lane][i].get());
      }
      morph->setTables(fx_tables_[FxTables::morphSlot(instance, false)].get(),
                       fx_tables_[FxTables::morphSlot(instance, true)].get());
      resetSpectralDrawings(lane);
    }
    effect_chain_->connectNote(modulation_handler_->bent_midi_output(), modulation_handler_->current_midi_output());

    filter_router_->setControls(filters_module_->getFilter1OnValue(), filters_module_->getFilter2OnValue(),
                                filters_module_->getFilter1FilterInputValue(),
                                filters_module_->getFilter2FilterInputValue());

    disableUnnecessaryModSources();
    setOversamplingAmount(kDefaultOversamplingAmount, kDefaultSampleRate);
  }

  void SoundEngine::connectModulation(const modulation_change& change) {
    change.modulation_processor->plug(change.source, ModulationConnectionProcessor::kModulationInput);
    change.modulation_processor->setDestinationScale(change.destination_scale);
    VITAL_ASSERT(vital::utils::isFinite(change.destination_scale));

    Processor* destination = change.mono_destination;
    bool polyphonic = change.source->owner->isPolyphonic() && change.poly_destination;
    change.modulation_processor->setPolyphonicModulation(polyphonic);
    if (polyphonic)
      destination = change.poly_destination;

    if (!destination->isControlRate() && !change.source->isControlRate()) {
      change.source->owner->setControlRate(false);
      change.modulation_processor->setControlRate(false);
    }

    change.source->owner->enable(true);
    change.modulation_processor->enable(true);
    destination->plugNext(change.modulation_processor);
    change.modulation_processor->process(1);
    destination->process(1);

    change.mono_modulation_switch->set(1);
    if (change.poly_modulation_switch)
      change.poly_modulation_switch->set(1);

    modulation_processors_.push_back(change.modulation_processor);
  }

  int SoundEngine::getNumPressedNotes() {
    return modulation_handler_->getNumPressedNotes();
  }

  void SoundEngine::disconnectModulation(const modulation_change& change) {
    Processor* destination = change.mono_destination;
    if (change.source->owner->isPolyphonic() && change.poly_destination)
      destination = change.poly_destination;

    destination->unplug(change.modulation_processor);

    if (change.mono_destination->connectedInputs() == 1 &&
        (change.poly_destination == nullptr || change.poly_destination->connectedInputs() == 0)) {
      change.mono_modulation_switch->set(0);

      if (change.poly_modulation_switch)
        change.poly_modulation_switch->set(0);
    }

    change.modulation_processor->enable(false);
    change.modulation_processor->setControlRate(true);
    if (change.num_audio_rate == 0)
      change.source->owner->setControlRate(true);
    modulation_processors_.remove(change.modulation_processor);
  }

  int SoundEngine::getNumActiveVoices() {
    return modulation_handler_->getNumActiveVoices();
  }

  ModulationConnectionBank& SoundEngine::getModulationBank() {
    return modulation_handler_->getModulationBank();
  }

  mono_float SoundEngine::getLastActiveNote() const {
    return modulation_handler_->getLastActiveNote();
  }

  void SoundEngine::setTuning(const Tuning* tuning) {
    modulation_handler_->setTuning(tuning);
  }

  void SoundEngine::checkOversampling() {
    int oversampling_amount = oversampling_->value();
    int sample_rate = getSampleRate();
    if (last_oversampling_amount_ != oversampling_amount || last_sample_rate_ != sample_rate) {
      setOversamplingAmount(1 << oversampling_amount, sample_rate);
      last_oversampling_amount_ = oversampling_amount;
      last_sample_rate_ = sample_rate;
    }
  }

  void SoundEngine::setOversamplingAmount(int oversampling_amount, int sample_rate) {
    static constexpr int kBaseSampleRate = 44100;
    int oversample = oversampling_amount;
    int sample_rate_mult = sample_rate / kBaseSampleRate;
    while (sample_rate_mult > 1 && oversample > 1) {
      sample_rate_mult >>= 1;
      oversample >>= 1;
    }

    upsampler_->setOversampleAmount(oversample);
    modulation_handler_->setOversampleAmount(oversample);
    effect_chain_->setOversampleAmount(oversample);
  }

  void SoundEngine::processWithInput(const poly_float* audio_in, int num_samples) {
    VITAL_ASSERT(num_samples <= output()->buffer_size);

    FloatVectorOperations::disableDenormalisedNumberSupport();
    modulation_handler_->setLegato(legato_->value());
    spectral_engaged_->set(spectralEngaged() ? 1.0f : 0.0f);

    audio_input_->processWithInput(audio_in, num_samples);
    sidechain_input_->processWithInput(sidechain_in_ ? sidechain_in_ : silence_, num_samples);
    ProcessorRouter::process(num_samples);
    applyPointModulation();

    for (auto& status_source : data_->status_outputs)
      status_source.second->update();
  }

  static_assert(SoundEngine::kNumPointMods == LineGenerator::kMaxPointSlots, "slot counts differ");

  LineGenerator* SoundEngine::getPointModCurve(int curve_id) {
    constexpr int kNumSpectral = kNumDrawingLanes * kNumSpectralCurves;
    if (curve_id < 0 || curve_id >= kNumPointModCurves)
      return nullptr;
    if (curve_id < kNumSpectral)
      return getSpectralCurve(curve_id / kNumSpectralCurves, curve_id % kNumSpectralCurves);
    return getLfoSource(curve_id - kNumSpectral);
  }

  int SoundEngine::bindPointMod(int curve_id, int point, int axis) {
    LineGenerator* curve = getPointModCurve(curve_id);
    if (curve == nullptr || point < 0 || point >= curve->getNumPoints())
      return -1;

    int existing = curve->pointSlot(point, axis);
    if (existing >= 0 && point_mod_curve_[existing] == curve_id)
      return existing;

    for (int slot = 0; slot < kNumPointMods; ++slot) {
      if (point_mod_curve_[slot] < 0) {
        point_mod_curve_[slot] = curve_id;
        curve->setPointSlot(point, axis, slot);
        return slot;
      }
    }
    return -1;
  }

  bool SoundEngine::bindPointModToSlot(int slot, int curve_id, int point, int axis) {
    LineGenerator* curve = getPointModCurve(curve_id);
    if (curve == nullptr || slot < 0 || slot >= kNumPointMods || point_mod_curve_[slot] >= 0 || point < 0 ||
        point >= curve->getNumPoints() || axis < 0 || axis >= LineGenerator::kNumAxes ||
        curve->pointSlot(point, axis) >= 0)
      return false;

    point_mod_curve_[slot] = curve_id;
    curve->setPointSlot(point, axis, slot);
    return true;
  }

  void SoundEngine::unbindPointMod(int slot) {
    if (slot < 0 || slot >= kNumPointMods || point_mod_curve_[slot] < 0)
      return;

    LineGenerator* curve = getPointModCurve(point_mod_curve_[slot]);
    point_mod_curve_[slot] = -1;
    int point, axis;
    if (curve && curve->findSlot(slot, point, axis)) {
      curve->setPointSlot(point, axis, -1);
      float zeros[kNumPointMods] = {};
      curve->applySlotValues(zeros);
    }
  }

  void SoundEngine::unbindAllPointMods() {
    for (int slot = 0; slot < kNumPointMods; ++slot)
      point_mod_curve_[slot] = -1;
    for (int id = 0; id < kNumPointModCurves; ++id) {
      if (LineGenerator* curve = getPointModCurve(id))
        curve->clearPointModulation();
    }
  }

  // Once per block, after the modulation matrix has run: moves the bound points by their slot's value.
  void SoundEngine::applyPointModulation() {
    float values[kNumPointMods];
    bool touched[kNumPointModCurves] = {};
    bool any = false;
    for (int slot = 0; slot < kNumPointMods; ++slot) {
      values[slot] = 0.0f;
      int id = point_mod_curve_[slot];
      if (id < 0)
        continue;
      LineGenerator* curve = getPointModCurve(id);
      if (curve == nullptr || !curve->hasSlot(slot)) {
        point_mod_curve_[slot] = -1;  // the point it was bound to is gone
        continue;
      }
      values[slot] = utils::clamp(modulation_handler_->pointModOutput(slot)->buffer[0][0], -1.0f, 1.0f);
      touched[id] = true;
      any = true;
    }

    if (!any && !had_point_modulation_)
      return;
    for (int id = 0; id < kNumPointModCurves; ++id) {
      if (touched[id] || point_mod_dirty_[id]) {
        getPointModCurve(id)->applySlotValues(values);
        point_mod_dirty_[id] = touched[id];
      }
    }
    had_point_modulation_ = any;
  }

  // Flat gain, a rising phase ramp and a flat (x1) comb delay: the starting point of each drawable mode.
  void SoundEngine::resetSpectralCurve(int lane, int index) {
    LineGenerator* curve = spectral_curves_[lane][index].get();
    curve->initLinear();
    if (lane >= kFirstFilterLane) {
      // Custom filter: A is a low pass, B a band pass, C a high pass (a point's y is upside down: 0 = pass).
      static const float kShapes[3][5][2] = {
        { { 0.0f, 0.0f }, { 0.35f, 0.0f }, { 0.7f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f } },
        { { 0.0f, 1.0f }, { 0.35f, 1.0f }, { 0.5f, 0.0f }, { 0.65f, 1.0f }, { 1.0f, 1.0f } },
        { { 0.0f, 1.0f }, { 0.3f, 1.0f }, { 0.65f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 0.0f } } };
      int count = index == 1 ? 5 : 4;
      curve->setNumPoints(count);
      for (int i = 0; i < count; ++i) {
        curve->setPoint(i, { kShapes[index][i][0], kShapes[index][i][1] });
        curve->setPower(i, 0.0f);
      }
      curve->setSmooth(true);
      curve->render();
      return;
    }
    if (index != 1) {
      float level = 0.5f;
      curve->setNumPoints(2);
      // (a line generator point is upside down: its value is 1 - y)
      curve->setPoint(0, { 0.0f, 1.0f - level });
      curve->setPoint(1, { 1.0f, 1.0f - level });
      curve->setPower(0, 0.0f);
      curve->setPower(1, 0.0f);
    }
    curve->render();
  }

  void SoundEngine::resetSpectralDrawings(int lane) {
    for (int i = 0; i < kNumSpectralCurves; ++i)
      resetSpectralCurve(lane, i);
    setRandomSeed(lane, uint32_t(SpectralMorphProcessor::kDefaultSeed));
  }

  uint32_t SoundEngine::getRandomSeed(int lane) const {
    if (lane >= kFirstFilterLane)
      return uint32_t(SpectralMorphProcessor::kDefaultSeed);
    if (lane >= 2)
      return static_cast<MorphModule*>(effect_chain_->getEffect(constants::kMorph, lane - 2))->seed();
    return spectral_[lane]->seed();
  }

  void SoundEngine::setRandomSeed(int lane, uint32_t seed) {
    if (lane >= kFirstFilterLane)
      return;
    if (lane >= 2) {
      static_cast<MorphModule*>(effect_chain_->getEffect(constants::kMorph, lane - 2))->setSeed(seed);
      return;
    }
    spectral_[lane]->setSeed(seed);
    spectral_alt_[lane]->setSeed(seed);
  }

  LineGenerator* SoundEngine::getSpectralCurve(int lane, int index) {
    return spectral_curves_[lane][index].get();
  }

  // The spectral stage only delays the audio while a spectral mode is chosen on either lane (the sidechain lane has
  // no Vocode). With both on None there is no latency at all.
  bool SoundEngine::spectralEngaged() const {
    if (input_spectral_mode_ == nullptr || sidechain_spectral_mode_ == nullptr)
      return false;
    int main_mode = static_cast<int>(input_spectral_mode_->value() + 0.5f);
    int side_mode = static_cast<int>(sidechain_spectral_mode_->value() + 0.5f);
    return main_mode != SpectralMorphProcessor::kNone || (side_mode != SpectralMorphProcessor::kNone &&
                                                          side_mode != SpectralMorphProcessor::kVocode);
  }

  int SoundEngine::getLatencySamples() const {
    if (fft_size_ == nullptr)
      return 0;
    int fft_index = static_cast<int>(fft_size_->value() + 0.5f);
    // The lanes and the spectral stage run at the host's rate (only the effect chain is oversampled); the Morph
    // effects in the chain add their own delay.
    int latency = spectralEngaged() ? SpectralMorphProcessor::fftSizeFromIndex(fft_index) : 0;
    if (effect_chain_ != nullptr)
      latency += effect_chain_->morphLatencySamples(fft_index);
    return latency;
  }

  const StereoMemory* SoundEngine::getInputMemory() { return input_memory_.get(); }

  const StereoMemory* SoundEngine::getSidechainMemory() { return sidechain_memory_.get(); }

  void SoundEngine::setSidechainInput(const poly_float* audio) {
    sidechain_in_ = audio;
  }

  void SoundEngine::correctToTime(double seconds) {
    modulation_handler_->correctToTime(seconds);
    effect_chain_->correctToTime(seconds);
  }

  void SoundEngine::allSoundsOff() {
    keytrack_notes_.allNotesOff();
    effect_chain_->hardReset();
  }

  void SoundEngine::allNotesOff(int sample) {
    keytrack_notes_.allNotesOff();
    modulation_handler_->allNotesOff(sample);
  }

  void SoundEngine::allNotesOff(int sample, int channel) {
    keytrack_notes_.allNotesOff();
    modulation_handler_->allNotesOff(channel);
  }

  void SoundEngine::allNotesOffRange(int sample, int from_channel, int to_channel) {
    keytrack_notes_.allNotesOff();
    modulation_handler_->allNotesOffRange(sample, from_channel, to_channel);
  }

  void SoundEngine::noteOn(int note, mono_float velocity, int sample, int channel) {
    keytrack_notes_.noteOn(note);
    modulation_handler_->noteOn(note, velocity, sample, channel);
  }

  void SoundEngine::noteOff(int note, mono_float lift, int sample, int channel) {
    keytrack_notes_.noteOff(note);
    modulation_handler_->noteOff(note, lift, sample, channel);
  }

  void SoundEngine::setModWheel(mono_float value, int channel) {
    modulation_handler_->setModWheel(value, channel);
  }

  void SoundEngine::setModWheelAllChannels(mono_float value) {
    modulation_handler_->setModWheelAllChannels(value);
  }
  
  void SoundEngine::setPitchWheel(mono_float value, int channel) {
    modulation_handler_->setPitchWheel(value, channel);
  }

  void SoundEngine::setZonedPitchWheel(mono_float value, int from_channel, int to_channel) {
    modulation_handler_->setZonedPitchWheel(value, from_channel, to_channel);
  }

  void SoundEngine::disableUnnecessaryModSources() {
    modulation_handler_->disableUnnecessaryModSources();
  }

  void SoundEngine::enableModSource(const std::string& source) {
    getModulationSource(source)->owner->enable(true);
  }

  void SoundEngine::disableModSource(const std::string& source) {
    modulation_handler_->disableModSource(source);
  }

  bool SoundEngine::isModSourceEnabled(const std::string& source) {
    return getModulationSource(source)->owner->enabled();
  }

  void SoundEngine::setEffectChain(const int* slots, int num_slots) {
    effect_chain_->setChain(slots, num_slots);
  }

  int SoundEngine::getEffectChain(int* slots) const {
    return effect_chain_->getChain(slots);
  }

  const StereoMemory* SoundEngine::getEqualizerMemory(int instance) {
    return effect_chain_->getEqualizerMemory(instance);
  }

  void SoundEngine::setAftertouch(mono_float note, mono_float value, int sample, int channel) {
    modulation_handler_->setAftertouch(note, value, sample, channel);
  }

  void SoundEngine::setChannelAftertouch(int channel, mono_float value, int sample) {
    modulation_handler_->setChannelAftertouch(channel, value, sample);
  }

  void SoundEngine::setChannelRangeAftertouch(int from_channel, int to_channel, mono_float value, int sample) {
    modulation_handler_->setChannelRangeAftertouch(from_channel, to_channel, value, sample);
  }

  void SoundEngine::setChannelSlide(int channel, mono_float value, int sample) {
    modulation_handler_->setChannelSlide(channel, value, sample);
  }

  void SoundEngine::setChannelRangeSlide(int from_channel, int to_channel, mono_float value, int sample) {
    modulation_handler_->setChannelRangeSlide(from_channel, to_channel, value, sample);
  }

  void SoundEngine::setBpm(mono_float bpm) {
    mono_float bps = bpm / 60.0f;
    if (bps_->value() != bps)
      bps_->set(bps);
  }

  Wavetable* SoundEngine::getWavetable(int index) {
    if (index < 0 || index >= kNumFxTables)
      return nullptr;
    return fx_tables_[index].get();
  }

  Sample* SoundEngine::getSample() {
    return nullptr;
  }

  LineGenerator* SoundEngine::getLfoSource(int index) {
    return modulation_handler_->getLfoSource(index);
  }

  void SoundEngine::sustainOn(int channel) {
    keytrack_notes_.sustainOn();
    modulation_handler_->sustainOn(channel);
  }

  void SoundEngine::sustainOff(int sample, int channel) {
    keytrack_notes_.sustainOff();
    modulation_handler_->sustainOff(sample, channel);
  }

  void SoundEngine::sostenutoOn(int channel) {
    modulation_handler_->sostenutoOn(channel);
  }

  void SoundEngine::sostenutoOff(int sample, int channel) {
    modulation_handler_->sostenutoOff(sample, channel);
  }

  void SoundEngine::sustainOnRange(int from_channel, int to_channel) {
    keytrack_notes_.sustainOn();
    modulation_handler_->sustainOnRange(from_channel, to_channel);
  }

  void SoundEngine::sustainOffRange(int sample, int from_channel, int to_channel) {
    keytrack_notes_.sustainOff();
    modulation_handler_->sustainOffRange(sample, from_channel, to_channel);
  }

  void SoundEngine::sostenutoOnRange(int from_channel, int to_channel) {
    modulation_handler_->sostenutoOnRange(from_channel, to_channel);
  }

  void SoundEngine::sostenutoOffRange(int sample, int from_channel, int to_channel) {
    modulation_handler_->sostenutoOffRange(sample, from_channel, to_channel);
  }
  bool SoundEngine::isMorphInUse(int instance) const {
    return effect_chain_ != nullptr && effect_chain_->morphInChain(instance);
  }

} // namespace vital
