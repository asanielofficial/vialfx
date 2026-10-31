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
#include "reorderable_effect_chain.h"
#include "value_switch.h"

namespace vital {

  SoundEngine::SoundEngine() : SynthModule(0, 1), modulation_handler_(nullptr), audio_input_(nullptr),
                               sidechain_input_(nullptr), sidechain_in_(nullptr), silence_(),
                               filter_router_(nullptr), filters_module_(nullptr), upsampler_(nullptr),
                               last_oversampling_amount_(-1), last_sample_rate_(-1), peak_meter_(nullptr) {
    // The four wavetables (input/sidechain warp table, input/sidechain spectral table) start as the built-in ones;
    // the editor can replace their contents. They exist before the graph, whose processors read them.
    for (int i = 0; i < kNumFxTables; ++i) {
      fx_tables_[i] = std::make_unique<Wavetable>(kNumOscillatorWaveFrames);
      FxTables::fillWavetable(fx_tables_[i].get(), i < 2 ? 0 : 1);
    }
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

    SpectralMorphProcessor* sidechain_spectral = new SpectralMorphProcessor(false);
    sidechain_spectral->plug(sidechain_gate, SpectralMorphProcessor::kAudio);
    sidechain_spectral->plug(sidechain_spectral_mode, SpectralMorphProcessor::kMode);
    sidechain_spectral->plug(sidechain_spectral_amount, SpectralMorphProcessor::kAmount);
    sidechain_spectral->plug(fft_size, SpectralMorphProcessor::kFftSize);
    sidechain_spectral->plug(spectral_engaged_, SpectralMorphProcessor::kEngaged);
    sidechain_spectral->plug(createMonoModControl("sidechain_spectral_table_mix"), SpectralMorphProcessor::kTableMix);
    sidechain_spectral->plug(sidechain_spectral_position, SpectralMorphProcessor::kPosition);
    sidechain_spectral->plug(sidechain_random_shuffle, SpectralMorphProcessor::kShuffle);
    sidechain_spectral->plug(sidechain_random_dice, SpectralMorphProcessor::kDice);
    sidechain_spectral->plug(createMonoModControl("sidechain_spectral_gain"), SpectralMorphProcessor::kGain);
    sidechain_spectral->plug(createMonoModControl("sidechain_spectral_density"), SpectralMorphProcessor::kDensity);
    sidechain_spectral->plug(createMonoModControl("sidechain_spectral_resonance"), SpectralMorphProcessor::kResonance);
    sidechain_spectral->plug(createMonoModControl("sidechain_spectral_spread"), SpectralMorphProcessor::kSpread);
    sidechain_spectral->plug(createMonoModControl("sidechain_spectral_depth"), SpectralMorphProcessor::kDepth);
    sidechain_spectral->plug(createMonoModControl("sidechain_spectral_phase"), SpectralMorphProcessor::kPhase);
    sidechain_spectral->setTable(fx_tables_[3].get());
    addProcessor(sidechain_spectral);

    SpectralMorphProcessor* main_spectral = new SpectralMorphProcessor(true);
    main_spectral->plug(main_lane, SpectralMorphProcessor::kAudio);
    main_spectral->plug(input_spectral_mode, SpectralMorphProcessor::kMode);
    main_spectral->plug(input_spectral_amount, SpectralMorphProcessor::kAmount);
    main_spectral->plug(fft_size, SpectralMorphProcessor::kFftSize);
    main_spectral->plug(spectral_engaged_, SpectralMorphProcessor::kEngaged);
    main_spectral->plug(createMonoModControl("input_spectral_table_mix"), SpectralMorphProcessor::kTableMix);
    main_spectral->plug(input_spectral_position, SpectralMorphProcessor::kPosition);
    main_spectral->plug(input_random_shuffle, SpectralMorphProcessor::kShuffle);
    main_spectral->plug(input_random_dice, SpectralMorphProcessor::kDice);
    main_spectral->plug(createMonoModControl("input_spectral_gain"), SpectralMorphProcessor::kGain);
    main_spectral->plug(createMonoModControl("input_spectral_density"), SpectralMorphProcessor::kDensity);
    main_spectral->plug(createMonoModControl("input_spectral_resonance"), SpectralMorphProcessor::kResonance);
    main_spectral->plug(createMonoModControl("input_spectral_spread"), SpectralMorphProcessor::kSpread);
    main_spectral->plug(createMonoModControl("input_spectral_depth"), SpectralMorphProcessor::kDepth);
    main_spectral->plug(createMonoModControl("input_spectral_phase"), SpectralMorphProcessor::kPhase);
    main_spectral->plug(sidechain_gate, SpectralMorphProcessor::kModulator);
    main_spectral->plug(createBaseControl("input_vocoder_bands"), SpectralMorphProcessor::kVocodeBands);
    main_spectral->plug(createBaseControl("input_vocoder_attack"), SpectralMorphProcessor::kVocodeAttack);
    main_spectral->plug(createBaseControl("input_vocoder_release"), SpectralMorphProcessor::kVocodeRelease);
    main_spectral->plug(createMonoModControl("input_vocoder_formant"), SpectralMorphProcessor::kVocodeFormant);
    main_spectral->plug(createMonoModControl("input_vocoder_flatten"), SpectralMorphProcessor::kVocodeFlatten);
    main_spectral->plug(createMonoModControl("input_vocoder_noise"), SpectralMorphProcessor::kVocodeNoise);
    main_spectral->plug(createMonoModControl("input_vocoder_emphasis"), SpectralMorphProcessor::kVocodeEmphasis);
    main_spectral->plug(createMonoModControl("input_vocoder_gate"), SpectralMorphProcessor::kVocodeGate);
    main_spectral->plug(createMonoModControl("input_vocoder_level"), SpectralMorphProcessor::kVocodeLevel);
    main_spectral->plug(createMonoModControl("input_vocoder_whiten"), SpectralMorphProcessor::kVocodeWhiten);
    main_spectral->setTable(fx_tables_[2].get());
    addProcessor(main_spectral);

    // Drawn curves (Spectral Filter / Phaser / Flanger) and Random Amplitudes seeds, per lane.
    spectral_[0] = main_spectral;
    spectral_[1] = sidechain_spectral;
    for (int lane = 0; lane < 2; ++lane) {
      for (int i = 0; i < kNumSpectralCurves; ++i) {
        spectral_curves_[lane][i] = std::make_unique<LineGenerator>();
        spectral_[lane]->setCurve(i, spectral_curves_[lane][i].get());
      }
      resetSpectralDrawings(lane);
    }

    // The sidechain lane warps first; the main lane's FM and RM modes use its result as their modulator.
    WaveWarp* sidechain_warp = new WaveWarp(false);
    sidechain_warp->plug(sidechain_spectral, WaveWarp::kAudio);
    sidechain_warp->plug(sidechain_warp_mode, WaveWarp::kMode);
    sidechain_warp->plug(sidechain_warp_amount, WaveWarp::kAmount);
    sidechain_warp->plug(createMonoModControl("sidechain_distortion_phase"), WaveWarp::kPhase);
    sidechain_warp->plug(createMonoModControl("sidechain_distortion_bias"), WaveWarp::kBias);
    sidechain_warp->plug(createMonoModControl("sidechain_distortion_smooth"), WaveWarp::kSmooth);
    sidechain_warp->plug(createMonoModControl("sidechain_distortion_drive"), WaveWarp::kDrive);
    sidechain_warp->plug(createBaseControl("sidechain_distortion_dc_filter"), WaveWarp::kDcFilter);
    sidechain_warp->plug(createMonoModControl("sidechain_distortion_mix"), WaveWarp::kMix);
    sidechain_warp->setTable(fx_tables_[1].get());
    addProcessor(sidechain_warp);

    AudioLane* sidechain_out = new AudioLane();
    sidechain_out->plug(sidechain_warp, AudioLane::kAudio);
    sidechain_out->plug(always_on, AudioLane::kOn);
    sidechain_out->plug(sidechain_level, AudioLane::kLevel);
    sidechain_out->plug(sidechain_pan, AudioLane::kPan);
    addProcessor(sidechain_out);

    WaveWarp* main_warp = new WaveWarp(true);
    main_warp->plug(createMonoModControl("input_distortion_phase"), WaveWarp::kPhase);
    main_warp->plug(createMonoModControl("input_distortion_bias"), WaveWarp::kBias);
    main_warp->plug(createMonoModControl("input_distortion_smooth"), WaveWarp::kSmooth);
    main_warp->plug(createMonoModControl("input_distortion_drive"), WaveWarp::kDrive);
    main_warp->plug(createBaseControl("input_distortion_dc_filter"), WaveWarp::kDcFilter);
    main_warp->plug(createMonoModControl("input_distortion_mix"), WaveWarp::kMix);
    main_warp->setTable(fx_tables_[0].get());
    main_warp->plug(main_spectral, WaveWarp::kAudio);
    main_warp->plug(input_warp_mode, WaveWarp::kMode);
    main_warp->plug(input_warp_amount, WaveWarp::kAmount);
    main_warp->plug(sidechain_warp, WaveWarp::kModulator);
    main_warp->plug(sidechain_on, WaveWarp::kModulatorOn);
    addProcessor(main_warp);

    // The input panels' spectrum displays show each lane after its level, pan and warp.
    input_memory_ = std::make_shared<StereoMemory>(kAudioMemorySamples);
    sidechain_memory_ = std::make_shared<StereoMemory>(kAudioMemorySamples);
    AudioTap* main_tap = new AudioTap(input_memory_.get());
    main_tap->plug(main_warp, AudioTap::kAudio);
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

    Add* filter_sum = new Add();
    filter_sum->plug(filters_module_, 0);
    filter_sum->plug(filter_router_->output(AudioFilterRouter::kRawOut), 1);
    // The upsampler reads this at the oversampled block size, so make the buffer big enough for that.
    filter_sum->output()->ensureBufferSize(kMaxBufferSize * kMaxOversample);
    addProcessor(filter_sum);

    upsampler_ = new AudioUpsampler();
    upsampler_->plug(filter_sum, AudioUpsampler::kAudio);
    addProcessor(upsampler_);

    Value* effect_chain_order = createBaseControl("effect_chain_order");
    effect_chain_ = new ReorderableEffectChain(beats_per_second, modulation_handler_->midi_offset_output());
    addSubmodule(effect_chain_);
    addProcessor(effect_chain_);
    effect_chain_->plug(upsampler_, ReorderableEffectChain::kAudio);
    effect_chain_->plug(effect_chain_order, ReorderableEffectChain::kOrder);

    SynthModule* compressor = effect_chain_->getEffect(constants::kCompressor);
    createStatusOutput("compressor_low_input", compressor->output(CompressorModule::kLowInputMeanSquared));
    createStatusOutput("compressor_band_input", compressor->output(CompressorModule::kBandInputMeanSquared));
    createStatusOutput("compressor_high_input", compressor->output(CompressorModule::kHighInputMeanSquared));
    createStatusOutput("compressor_low_output", compressor->output(CompressorModule::kLowOutputMeanSquared));
    createStatusOutput("compressor_band_output", compressor->output(CompressorModule::kBandOutputMeanSquared));
    createStatusOutput("compressor_high_output", compressor->output(CompressorModule::kHighOutputMeanSquared));

    SynthModule* chorus = effect_chain_->getEffect(constants::kChorus);
    for (int i = 0; i < ChorusModule::kMaxDelayPairs; ++i)
      createStatusOutput("chorus_delays" + std::to_string(i + 1), chorus->output(i + 1));

    SynthModule* phaser = effect_chain_->getEffect(constants::kPhaser);
    createStatusOutput("phaser_cutoff", phaser->output(PhaserModule::kCutoffOutput));

    SynthModule* flanger = effect_chain_->getEffect(constants::kFlanger);
    createStatusOutput("flanger_delay_frequency", flanger->output(FlangerModule::kFrequencyOutput));

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

    for (auto& status_source : data_->status_outputs)
      status_source.second->update();
  }

  // Flat gain, a rising phase ramp and a flat (x1) comb delay: the starting point of each drawable mode.
  void SoundEngine::resetSpectralCurve(int lane, int index) {
    LineGenerator* curve = spectral_curves_[lane][index].get();
    curve->initLinear();
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

  uint32_t SoundEngine::getRandomSeed(int lane) const { return spectral_[lane]->seed(); }

  void SoundEngine::setRandomSeed(int lane, uint32_t seed) {
    spectral_[lane]->setSeed(seed);
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
    if (fft_size_ == nullptr || !spectralEngaged())
      return 0;
    // The lanes and the spectral stage run at the host's rate (only the effect chain is oversampled).
    return SpectralMorphProcessor::fftSizeFromIndex(static_cast<int>(fft_size_->value() + 0.5f));
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
    effect_chain_->hardReset();
  }

  void SoundEngine::allNotesOff(int sample) {
    modulation_handler_->allNotesOff(sample);
  }

  void SoundEngine::allNotesOff(int sample, int channel) {
    modulation_handler_->allNotesOff(channel);
  }

  void SoundEngine::allNotesOffRange(int sample, int from_channel, int to_channel) {
    modulation_handler_->allNotesOffRange(sample, from_channel, to_channel);
  }

  void SoundEngine::noteOn(int note, mono_float velocity, int sample, int channel) {
    modulation_handler_->noteOn(note, velocity, sample, channel);
  }

  void SoundEngine::noteOff(int note, mono_float lift, int sample, int channel) {
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

  const StereoMemory* SoundEngine::getEqualizerMemory() {
    return effect_chain_->getEqualizerMemory();
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
    modulation_handler_->sustainOn(channel);
  }

  void SoundEngine::sustainOff(int sample, int channel) {
    modulation_handler_->sustainOff(sample, channel);
  }

  void SoundEngine::sostenutoOn(int channel) {
    modulation_handler_->sostenutoOn(channel);
  }

  void SoundEngine::sostenutoOff(int sample, int channel) {
    modulation_handler_->sostenutoOff(sample, channel);
  }

  void SoundEngine::sustainOnRange(int from_channel, int to_channel) {
    modulation_handler_->sustainOnRange(from_channel, to_channel);
  }

  void SoundEngine::sustainOffRange(int sample, int from_channel, int to_channel) {
    modulation_handler_->sustainOffRange(sample, from_channel, to_channel);
  }

  void SoundEngine::sostenutoOnRange(int from_channel, int to_channel) {
    modulation_handler_->sostenutoOnRange(from_channel, to_channel);
  }

  void SoundEngine::sostenutoOffRange(int sample, int from_channel, int to_channel) {
    modulation_handler_->sostenutoOffRange(sample, from_channel, to_channel);
  }
} // namespace vital
