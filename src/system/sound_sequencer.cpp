// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sound_sequencer.h"

#include "settings_manager.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace helix;

SoundSequencer::SoundSequencer(std::shared_ptr<SoundBackend> backend)
    : backend_(std::move(backend)) {}

SoundSequencer::~SoundSequencer() {
    shutdown();
}

void SoundSequencer::play(const SoundDefinition& sound, SoundPriority priority) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    request_queue_.push({sound, priority});
    queue_cv_.notify_one();
}

void SoundSequencer::stop() {
    stop_requested_.store(true);
    queue_cv_.notify_one();
}

bool SoundSequencer::is_playing() const {
    return playing_.load();
}

void SoundSequencer::start() {
    if (running_.load())
        return;
    running_.store(true);
    sequencer_thread_ = std::thread(&SoundSequencer::sequencer_loop, this);
    spdlog::debug("[SoundSequencer] started sequencer thread");
}

void SoundSequencer::shutdown() {
    if (!running_.load())
        return;
    running_.store(false);
    queue_cv_.notify_one();
    if (sequencer_thread_.joinable()) {
        sequencer_thread_.join();
    }
    spdlog::debug("[SoundSequencer] shutdown complete");
}

void SoundSequencer::sequencer_loop() {
    spdlog::debug("[SoundSequencer] sequencer loop started");

    auto last_tick = std::chrono::steady_clock::now();
    bool was_playing = false;

    // Respect backend's minimum tick interval for sleep duration
    const float min_tick = backend_ ? backend_->min_tick_ms() : 1.0f;
    const auto tick_interval =
        std::chrono::microseconds(static_cast<int>(std::max(1.0f, min_tick) * 1000.0f));

    while (running_.load()) {
        // Check for stop request
        if (stop_requested_.load()) {
            stop_requested_.store(false);
            if (playing_.load()) {
                end_playback();
                was_playing = false;
            }
        }

        // Check queue for new requests
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            if (!playing_.load() && request_queue_.empty()) {
                // Nothing playing, nothing queued — wait for a signal
                was_playing = false;
                queue_cv_.wait_for(lock, std::chrono::milliseconds(10));
                last_tick = std::chrono::steady_clock::now();
                continue;
            }

            // Process all queued requests — last one at highest priority wins
            while (!request_queue_.empty()) {
                auto& req = request_queue_.front();

                if (!playing_.load()) {
                    // Not playing — start this sound
                    begin_playback(std::move(req));
                    request_queue_.pop();
                } else if (static_cast<int>(req.priority) >= static_cast<int>(current_priority_)) {
                    // Higher or equal priority — preempt
                    end_playback();
                    begin_playback(std::move(req));
                    request_queue_.pop();
                } else {
                    // Lower priority — drop it
                    request_queue_.pop();
                }
            }
        }

        // Tick if playing
        if (playing_.load()) {
            // Reset last_tick when transitioning to playing to avoid
            // counting queue processing time as elapsed playback time
            if (!was_playing) {
                last_tick = std::chrono::steady_clock::now();
                was_playing = true;
            }

            auto now = std::chrono::steady_clock::now();
            float dt_ms = std::chrono::duration<float, std::milli>(now - last_tick).count();
            last_tick = now;

            // Cap dt to avoid huge jumps from scheduling delays
            dt_ms = std::min(dt_ms, 5.0f);

            tick(dt_ms);
        } else {
            was_playing = false;
            last_tick = std::chrono::steady_clock::now();
        }

        // Sleep for backend's minimum tick interval
        std::this_thread::sleep_for(tick_interval);
    }

    // Clean shutdown
    if (playing_.load()) {
        end_playback();
    }
}

void SoundSequencer::tick(float dt_ms) {
    if (!playing_.load())
        return;

    auto& steps = current_sound_.steps;
    if (step_state_.step_index >= static_cast<int>(steps.size())) {
        // Past the end — check repeat
        advance_step();
        return;
    }

    auto& step = steps[step_state_.step_index];

    // Advance elapsed time
    step_state_.elapsed_ms += dt_ms;

    // Check if this step is complete
    if (step_state_.elapsed_ms >= step_state_.total_ms) {
        advance_step();
        return;
    }

    // Process the step
    if (step.is_pause) {
        backend_->silence();
        return;
    }

    float elapsed = step_state_.elapsed_ms;
    float duration = step_state_.total_ms;
    float progress = (duration > 0) ? std::clamp(elapsed / duration, 0.0f, 1.0f) : 1.0f;

    // Base values
    float freq = step.freq_hz;
    float amplitude = step.velocity;
    float duty = 0.5f; // default duty cycle for square wave

    // Apply ADSR envelope
    float env_mul = compute_envelope(step.envelope, elapsed, duration);
    amplitude *= env_mul;

    // Apply sweep
    if (!step.sweep.target.empty() && step.sweep.target == "freq") {
        freq = compute_sweep(step.freq_hz, step.sweep.end_value, progress);
    }

    // Apply LFO
    if (step.lfo.rate > 0 && step.lfo.depth > 0) {
        float lfo_val = compute_lfo(step.lfo, elapsed);
        if (step.lfo.target == "freq") {
            freq += lfo_val;
        } else if (step.lfo.target == "amplitude") {
            amplitude += lfo_val;
        } else if (step.lfo.target == "duty") {
            duty += lfo_val;
        }
    }

    // Apply master volume attenuation
    float volume = SettingsManager::instance().get_volume() / 100.0f;
    amplitude *= volume;

    // Clamp outputs
    freq = std::clamp(freq, 20.0f, 20000.0f);
    amplitude = std::clamp(amplitude, 0.0f, 1.0f);
    duty = std::clamp(duty, 0.0f, 1.0f);

    // Set waveform if backend supports it
    if (backend_->supports_waveforms()) {
        backend_->set_waveform(step.wave);
    }

    // Set filter if backend supports it and step has a filter configured
    if (backend_->supports_filter() && !step.filter.type.empty()) {
        float cutoff = step.filter.cutoff;
        if (step.filter.sweep_to > 0) {
            cutoff = compute_sweep(step.filter.cutoff, step.filter.sweep_to, progress);
        }
        backend_->set_filter(step.filter.type, cutoff);
    }

    backend_->set_tone(freq, amplitude, duty);
}

void SoundSequencer::advance_step() {
    step_state_.step_index++;

    if (step_state_.step_index >= static_cast<int>(current_sound_.steps.size())) {
        // Sequence complete — check repeat
        step_state_.repeat_remaining--;
        if (step_state_.repeat_remaining > 0) {
            // Restart the sequence
            step_state_.step_index = 0;
            step_state_.elapsed_ms = 0;
            if (!current_sound_.steps.empty()) {
                auto& step = current_sound_.steps[0];
                float env_min =
                    step.envelope.attack_ms + step.envelope.decay_ms + step.envelope.release_ms;
                step_state_.total_ms = std::max(step.duration_ms, env_min);
            }
            // Silence between repeats
            backend_->silence();
            return;
        }

        // Done playing
        end_playback();
        return;
    }

    // Set up the next step
    step_state_.elapsed_ms = 0;
    auto& step = current_sound_.steps[step_state_.step_index];
    float env_min = step.envelope.attack_ms + step.envelope.decay_ms + step.envelope.release_ms;
    step_state_.total_ms = std::max(step.duration_ms, env_min);

    // Skip zero-duration steps
    if (step_state_.total_ms <= 0) {
        advance_step();
        return;
    }

    // Silence at step boundary
    backend_->silence();
}

float SoundSequencer::compute_envelope(const ADSREnvelope& env, float elapsed_ms,
                                       float duration_ms) const {
    float a = env.attack_ms;
    float d = env.decay_ms;
    float s = env.sustain_level;
    float r = env.release_ms;

    // If all ADSR times are 0, return full amplitude
    if (a <= 0 && d <= 0 && r <= 0)
        return 1.0f;

    // Release starts at (duration_ms - release_ms)
    float release_start = duration_ms - r;

    if (elapsed_ms < a) {
        // Attack phase: ramp 0 -> 1
        return (a > 0) ? (elapsed_ms / a) : 1.0f;
    } else if (elapsed_ms < a + d) {
        // Decay phase: ramp 1 -> sustain
        float decay_progress = (d > 0) ? ((elapsed_ms - a) / d) : 1.0f;
        return 1.0f - (1.0f - s) * decay_progress;
    } else if (elapsed_ms < release_start) {
        // Sustain phase: hold at sustain level
        return s;
    } else {
        // Release phase: ramp sustain -> 0
        float release_elapsed = elapsed_ms - release_start;
        float release_progress = (r > 0) ? std::clamp(release_elapsed / r, 0.0f, 1.0f) : 1.0f;
        return s * (1.0f - release_progress);
    }
}

float SoundSequencer::compute_lfo(const LFOParams& lfo, float elapsed_ms) const {
    if (lfo.rate <= 0)
        return 0.0f;
    // Sinusoidal modulation
    float phase = 2.0f * static_cast<float>(M_PI) * lfo.rate * elapsed_ms / 1000.0f;
    return std::sin(phase) * lfo.depth;
}

float SoundSequencer::compute_sweep(float start, float end, float progress) const {
    return start + (end - start) * progress;
}

void SoundSequencer::begin_playback(PlayRequest&& req) {
    current_sound_ = std::move(req.sound);
    current_priority_ = req.priority;

    // Skip empty sequences
    if (current_sound_.steps.empty()) {
        return;
    }

    step_state_ = {};
    step_state_.step_index = 0;
    step_state_.repeat_remaining = std::max(1, current_sound_.repeat);
    step_state_.elapsed_ms = 0;

    auto& step = current_sound_.steps[0];
    float env_min = step.envelope.attack_ms + step.envelope.decay_ms + step.envelope.release_ms;
    step_state_.total_ms = std::max(step.duration_ms, env_min);

    // Skip zero-duration first step
    if (step_state_.total_ms <= 0) {
        advance_step();
        if (step_state_.step_index >= static_cast<int>(current_sound_.steps.size()) &&
            step_state_.repeat_remaining <= 0) {
            return; // Entire sequence was zero-duration
        }
    }

    playing_.store(true);
    spdlog::debug("[SoundSequencer] begin playback: {} ({} steps, {} repeats)", current_sound_.name,
                  current_sound_.steps.size(), step_state_.repeat_remaining);
}

void SoundSequencer::end_playback() {
    backend_->silence();
    playing_.store(false);
    spdlog::debug("[SoundSequencer] end playback");
}
