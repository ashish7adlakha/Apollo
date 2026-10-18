<script setup>
import { ref, onMounted, onUnmounted, computed, inject } from 'vue'
import Checkbox from '../../../Checkbox.vue'

const $t = inject('i18n').t

const props = defineProps([
  'platform',
  'config'
])

const isToolOpen = ref(false)
const isLoading = ref(false)
const saveSuccess = ref(false)

const enabled = ref(props.config.hdr_calibration_enabled === true || props.config.hdr_calibration_enabled === 'true')
const testPatternActive = ref(false)
const testPatternNits = ref(10.0)
const testPatternFullscreen = ref(false)

const points = ref([
  { nits: 0.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 0.5, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 1.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 2.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 5.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 10.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 25.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 50.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 100.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 200.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 400.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 800.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 1000.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 1200.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 2000.0, r: 1.0, g: 1.0, b: 1.0 },
  { nits: 4000.0, r: 1.0, g: 1.0, b: 1.0 }
])

const selectedNits = ref(10.0)
const newCustomNits = ref(150.0)

const activePoint = computed(() => {
  let pt = points.value.find(p => Math.abs(p.nits - selectedNits.value) < 0.01)
  if (!pt && points.value.length > 0) {
    pt = points.value[0]
  }
  return pt
})

const formatMultiplier = (val) => {
  if (typeof val !== 'number' || isNaN(val)) return '1.000'
  return val.toFixed(3)
}

const formatOffsetPercent = (val) => {
  if (typeof val !== 'number' || isNaN(val)) return '0.0%'
  const pct = (val - 1.0) * 100.0
  return (pct >= 0 ? '+' : '') + pct.toFixed(1) + '%'
}

const fetchCalibration = async () => {
  try {
    isLoading.value = true
    const res = await fetch('/api/hdr-calibration')
    if (res.ok) {
      const data = await res.json()
      enabled.value = !!data.enabled
      props.config.hdr_calibration_enabled = enabled.value
      testPatternActive.value = !!data.test_pattern_active
      if (testPatternActive.value) {
        startPatternPolling()
      }
      testPatternNits.value = data.test_pattern_nits ?? 10.0
      testPatternFullscreen.value = !!data.test_pattern_fullscreen
      if (Array.isArray(data.points) && data.points.length > 0) {
        points.value = data.points.map(pt => ({
          nits: pt.nits,
          r: typeof pt.r === 'number' ? pt.r : 1.0,
          g: typeof pt.g === 'number' ? pt.g : 1.0,
          b: typeof pt.b === 'number' ? pt.b : 1.0
        }))
      }
    }
  } catch (err) {
    console.warn('Failed to load HDR calibration:', err)
  } finally {
    isLoading.value = false
  }
}

let pollTimer = null

const startPatternPolling = () => {
  stopPatternPolling()
  pollTimer = setInterval(async () => {
    if (!testPatternActive.value) {
      stopPatternPolling()
      return
    }
    try {
      const res = await fetch('/api/hdr-calibration')
      if (res.ok) {
        const data = await res.json()
        if (data.test_pattern_active === false) {
          testPatternActive.value = false
          stopPatternPolling()
        }
      }
    } catch (_) {}
  }, 750)
}

const stopPatternPolling = () => {
  if (pollTimer) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

const handleGlobalKeydown = (e) => {
  if (testPatternActive.value && (e.key === 'Escape' || e.key === ' ')) {
    if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA')) {
      return
    }
    testPatternActive.value = false
    stopPatternPolling()
    sendLiveUpdate()
  }
}

onMounted(() => {
  fetchCalibration()
  window.addEventListener('keydown', handleGlobalKeydown)
})

onUnmounted(() => {
  stopPatternPolling()
  window.removeEventListener('keydown', handleGlobalKeydown)
})

let isUpdating = false
let updatePending = false

const sendLiveUpdate = async () => {
  if (isUpdating) {
    updatePending = true
    return
  }
  isUpdating = true
  try {
    do {
      updatePending = false
      await fetch('/api/hdr-calibration', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          enabled: enabled.value,
          test_pattern_active: testPatternActive.value,
          test_pattern_nits: testPatternNits.value,
          test_pattern_fullscreen: testPatternFullscreen.value,
          points: points.value
        })
      })
    } while (updatePending)
  } catch (err) {
    console.error('Failed to send live calibration update:', err)
  } finally {
    isUpdating = false
  }
}

const toggleEnabled = () => {
  props.config.hdr_calibration_enabled = enabled.value
  sendLiveUpdate()
}

const toggleTestPattern = () => {
  testPatternActive.value = !testPatternActive.value
  if (testPatternActive.value) {
    startPatternPolling()
  } else {
    stopPatternPolling()
  }
  sendLiveUpdate()
}

const toggleFullscreenPattern = () => {
  testPatternFullscreen.value = !testPatternFullscreen.value
  sendLiveUpdate()
}

const selectCheckpoint = (nits) => {
  selectedNits.value = nits
  testPatternNits.value = nits
  sendLiveUpdate()
}

const onSliderChange = () => {
  sendLiveUpdate()
}

const resetChannel = (channel) => {
  if (activePoint.value) {
    activePoint.value[channel] = 1.0
    sendLiveUpdate()
  }
}

const resetActivePoint = () => {
  if (activePoint.value) {
    activePoint.value.r = 1.0
    activePoint.value.g = 1.0
    activePoint.value.b = 1.0
    sendLiveUpdate()
  }
}

const resetAllPoints = () => {
  points.value.forEach(p => {
    p.r = 1.0
    p.g = 1.0
    p.b = 1.0
  })
  sendLiveUpdate()
}

const addCustomPoint = () => {
  const nitsVal = parseFloat(newCustomNits.value)
  if (isNaN(nitsVal) || nitsVal < 0) return
  if (!points.value.some(p => Math.abs(p.nits - nitsVal) < 0.01)) {
    points.value.push({ nits: nitsVal, r: 1.0, g: 1.0, b: 1.0 })
    points.value.sort((a, b) => a.nits - b.nits)
    selectCheckpoint(nitsVal)
  }
}

const removePoint = (nits) => {
  if (points.value.length <= 2) return // keep at least 2 points
  points.value = points.value.filter(p => Math.abs(p.nits - nits) >= 0.01)
  if (Math.abs(selectedNits.value - nits) < 0.01) {
    selectedNits.value = points.value[0].nits
  }
  sendLiveUpdate()
}

const saveProfile = async () => {
  try {
    saveSuccess.value = false
    const res = await fetch('/api/hdr-calibration/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        enabled: enabled.value,
        points: points.value
      })
    })
    if (res.ok) {
      saveSuccess.value = true
      props.config.hdr_calibration_enabled = enabled.value
      props.config.hdr_calibration_data = JSON.stringify(points.value)
      setTimeout(() => {
        saveSuccess.value = false
      }, 4000)
    }
  } catch (err) {
    console.error('Failed to save HDR calibration profile:', err)
  }
}
</script>

<template>
  <div class="mb-4 p-3 border rounded bg-body-tertiary">
    <div class="d-flex align-items-center justify-content-between mb-2">
      <div>
        <h5 class="mb-1 text-primary">
          <i class="fa-solid fa-palette me-2"></i>{{ $t('config.hdr_calibration') }}
        </h5>
        <div class="form-text mt-0">{{ $t('config.hdr_calibration_desc') }}</div>
      </div>
      <div class="form-check form-switch ms-3">
        <input
          class="form-check-input"
          type="checkbox"
          role="switch"
          id="hdr_calibration_enabled_toggle"
          v-model="enabled"
          @change="toggleEnabled"
        />
      </div>
    </div>

    <div class="mt-2">
      <button
        class="btn btn-outline-primary btn-sm"
        type="button"
        @click="isToolOpen = !isToolOpen"
      >
        <i class="fa-solid" :class="isToolOpen ? 'fa-chevron-up' : 'fa-sliders'"></i>
        {{ isToolOpen ? 'Close HDR Calibration Studio' : $t('config.hdr_calibration_tool') }}
      </button>
    </div>

    <!-- Interactive Calibration Panel -->
    <div v-if="isToolOpen" class="mt-3 p-3 border rounded bg-body">
      <!-- Live Status Alert -->
      <div class="alert alert-info py-2 d-flex align-items-center justify-content-between">
        <div>
          <i class="fa-solid fa-circle-check text-success me-2"></i>
          <strong>Real-Time Stream Calibration:</strong> Multiplier adjustments apply immediately to your active HDR video stream.
        </div>
        <div v-if="saveSuccess" class="badge bg-success py-2 px-3">
          <i class="fa-solid fa-floppy-disk me-1"></i> Saved to config!
        </div>
      </div>

      <!-- Test Pattern Controls Bar -->
      <div class="card mb-3 shadow-sm">
        <div class="card-header bg-primary bg-opacity-10 py-2 d-flex justify-content-between align-items-center">
          <strong class="text-primary"><i class="fa-solid fa-bullseye me-2"></i>In-Stream Test Pattern Generator</strong>
          <div class="d-flex align-items-center gap-1">
            <span class="badge" :class="testPatternActive ? 'bg-success' : 'bg-secondary'">
              {{ testPatternActive ? 'Pattern Active on Stream' : 'Pattern Inactive (Showing Stream)' }}
            </span>
            <span v-if="testPatternActive" class="badge bg-light text-dark border">
              <i class="fa-solid fa-keyboard me-1"></i>Any key / click hides
            </span>
          </div>
        </div>
        <div class="card-body">
          <div class="d-flex flex-wrap gap-2 align-items-center mb-3">
            <button
              class="btn btn-sm"
              :class="testPatternActive ? 'btn-danger' : 'btn-success'"
              type="button"
              @click="toggleTestPattern"
            >
              <i class="fa-solid" :class="testPatternActive ? 'fa-stop me-1' : 'fa-play me-1'"></i>
              {{ testPatternActive ? 'Hide Test Pattern (Resume Stream)' : 'Show Test Pattern on Stream' }}
            </button>

            <button
              class="btn btn-sm btn-outline-secondary"
              type="button"
              @click="toggleFullscreenPattern"
              :disabled="!testPatternActive"
            >
              <i class="fa-solid fa-expand me-1"></i>
              {{ testPatternFullscreen ? 'Mode: Full Screen' : 'Mode: 10% Window (ABL Safe)' }}
            </button>

            <span class="text-muted small ms-2">
              (Use 10% window to avoid OLED/Mini-LED ABL brightness throttling at high nits)
            </span>
          </div>

          <!-- Quick Checkpoint Selector Buttons -->
          <div>
            <div class="small fw-semibold text-muted mb-1">Quick Select Target Luminance (Nits):</div>
            <div class="d-flex flex-wrap gap-1">
              <button
                v-for="pt in points"
                :key="pt.nits"
                type="button"
                class="btn btn-sm"
                :class="[
                  Math.abs(selectedNits - pt.nits) < 0.01 ? 'btn-primary' : 'btn-outline-secondary',
                  (pt.nits === 10 || pt.nits === 1200) ? 'border-2 fw-bold' : ''
                ]"
                @click="selectCheckpoint(pt.nits)"
              >
                {{ pt.nits }} nits
                <span v-if="pt.nits === 10" class="badge bg-warning text-dark ms-1">Shadows</span>
                <span v-if="pt.nits === 1200" class="badge bg-warning text-dark ms-1">Highlights</span>
              </button>
            </div>
          </div>
        </div>
      </div>

      <!-- Active Point Color Tuning -->
      <div v-if="activePoint" class="card mb-3 shadow-sm border-primary">
        <div class="card-header bg-primary text-white py-2 d-flex justify-content-between align-items-center">
          <div>
            <strong>Tuning Grayscale Balance at: {{ activePoint.nits }} Nits</strong>
            <span v-if="activePoint.nits === 10" class="badge bg-light text-dark ms-2">Neutralize Shadow Pink Shift</span>
            <span v-if="activePoint.nits === 1200" class="badge bg-light text-dark ms-2">Neutralize Highlight Pink Shift</span>
          </div>
          <button class="btn btn-sm btn-light py-0" type="button" @click="resetActivePoint">
            <i class="fa-solid fa-rotate-left me-1"></i>Reset Point (1.0x)
          </button>
        </div>
        <div class="card-body">
          <div class="row g-3">
            <!-- Red Channel -->
            <div class="col-12 col-md-4">
              <div class="d-flex justify-content-between align-items-center mb-1">
                <label class="form-label mb-0 text-danger fw-bold">
                  <i class="fa-solid fa-circle me-1"></i>Red Multiplier:
                </label>
                <div class="small">
                  <span class="badge bg-danger bg-opacity-10 text-danger border border-danger-subtle font-monospace me-1">
                    {{ formatMultiplier(activePoint.r) }}x
                  </span>
                  <span class="text-muted">({{ formatOffsetPercent(activePoint.r) }})</span>
                </div>
              </div>
              <div class="d-flex align-items-center gap-2">
                <input
                  type="range"
                  class="form-range flex-grow-1"
                  min="0.000"
                  max="2.000"
                  step="0.005"
                  v-model.number="activePoint.r"
                  @input="onSliderChange"
                />
                <div class="input-group input-group-sm" style="width: 105px;">
                  <input
                    type="number"
                    class="form-control form-control-sm text-end font-monospace px-1"
                    min="0"
                    max="3"
                    step="0.005"
                    v-model.number="activePoint.r"
                    @input="onSliderChange"
                  />
                  <span class="input-group-text px-1">x</span>
                </div>
                <button
                  class="btn btn-outline-secondary btn-sm py-0 px-2"
                  type="button"
                  title="Reset Red to 1.0x"
                  @click="resetChannel('r')"
                >
                  1.0
                </button>
              </div>
            </div>

            <!-- Green Channel -->
            <div class="col-12 col-md-4">
              <div class="d-flex justify-content-between align-items-center mb-1">
                <label class="form-label mb-0 text-success fw-bold">
                  <i class="fa-solid fa-circle me-1"></i>Green Multiplier:
                </label>
                <div class="small">
                  <span class="badge bg-success bg-opacity-10 text-success border border-success-subtle font-monospace me-1">
                    {{ formatMultiplier(activePoint.g) }}x
                  </span>
                  <span class="text-muted">({{ formatOffsetPercent(activePoint.g) }})</span>
                </div>
              </div>
              <div class="d-flex align-items-center gap-2">
                <input
                  type="range"
                  class="form-range flex-grow-1"
                  min="0.000"
                  max="2.000"
                  step="0.005"
                  v-model.number="activePoint.g"
                  @input="onSliderChange"
                />
                <div class="input-group input-group-sm" style="width: 105px;">
                  <input
                    type="number"
                    class="form-control form-control-sm text-end font-monospace px-1"
                    min="0"
                    max="3"
                    step="0.005"
                    v-model.number="activePoint.g"
                    @input="onSliderChange"
                  />
                  <span class="input-group-text px-1">x</span>
                </div>
                <button
                  class="btn btn-outline-secondary btn-sm py-0 px-2"
                  type="button"
                  title="Reset Green to 1.0x"
                  @click="resetChannel('g')"
                >
                  1.0
                </button>
              </div>
            </div>

            <!-- Blue Channel -->
            <div class="col-12 col-md-4">
              <div class="d-flex justify-content-between align-items-center mb-1">
                <label class="form-label mb-0 text-info fw-bold">
                  <i class="fa-solid fa-circle me-1"></i>Blue Multiplier:
                </label>
                <div class="small">
                  <span class="badge bg-info bg-opacity-10 text-info border border-info-subtle font-monospace me-1">
                    {{ formatMultiplier(activePoint.b) }}x
                  </span>
                  <span class="text-muted">({{ formatOffsetPercent(activePoint.b) }})</span>
                </div>
              </div>
              <div class="d-flex align-items-center gap-2">
                <input
                  type="range"
                  class="form-range flex-grow-1"
                  min="0.000"
                  max="2.000"
                  step="0.005"
                  v-model.number="activePoint.b"
                  @input="onSliderChange"
                />
                <div class="input-group input-group-sm" style="width: 105px;">
                  <input
                    type="number"
                    class="form-control form-control-sm text-end font-monospace px-1"
                    min="0"
                    max="3"
                    step="0.005"
                    v-model.number="activePoint.b"
                    @input="onSliderChange"
                  />
                  <span class="input-group-text px-1">x</span>
                </div>
                <button
                  class="btn btn-outline-secondary btn-sm py-0 px-2"
                  type="button"
                  title="Reset Blue to 1.0x"
                  @click="resetChannel('b')"
                >
                  1.0
                </button>
              </div>
            </div>
          </div>
          <div class="form-text mt-2">
            <i class="fa-solid fa-info-circle me-1"></i>
            Tip: Adjust using either the sliders or type exact multipliers directly (e.g. <code>0.945</code> to reduce red, <code>1.030</code> to boost green) until the grey pattern appears completely neutral.
          </div>
        </div>
      </div>

      <!-- Checkpoints Table -->
      <div class="card mb-3 shadow-sm">
        <div class="card-header bg-body-secondary py-2 d-flex justify-content-between align-items-center">
          <strong class="small text-uppercase"><i class="fa-solid fa-list-ol me-2"></i>All Calibration Checkpoints</strong>
          <div class="d-flex align-items-center gap-2">
            <input
              type="number"
              class="form-control form-control-sm"
              style="width: 100px;"
              placeholder="Nits"
              min="0.1"
              max="10000"
              step="1"
              v-model="newCustomNits"
            />
            <button class="btn btn-sm btn-outline-primary text-nowrap" type="button" @click="addCustomPoint">
              <i class="fa-solid fa-plus me-1"></i>Add Point
            </button>
          </div>
        </div>
        <div class="table-responsive" style="max-height: 240px; overflow-y: auto;">
          <table class="table table-sm table-hover align-middle mb-0">
            <thead class="table-light sticky-top">
              <tr>
                <th>Luminance</th>
                <th>Red</th>
                <th>Green</th>
                <th>Blue</th>
                <th class="text-end">Actions</th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="pt in points"
                :key="pt.nits"
                :class="Math.abs(selectedNits - pt.nits) < 0.01 ? 'table-primary fw-bold' : ''"
                style="cursor: pointer;"
                @click="selectCheckpoint(pt.nits)"
              >
                <td>
                  {{ pt.nits }} nits
                  <span v-if="pt.nits === 10" class="badge bg-warning text-dark ms-1">Shadows</span>
                  <span v-if="pt.nits === 1200" class="badge bg-warning text-dark ms-1">Highlights</span>
                </td>
                <td :class="Math.abs(pt.r - 1.0) > 0.001 ? 'text-danger fw-bold' : 'text-muted'">
                  {{ formatMultiplier(pt.r) }}x
                  <span class="small text-muted" v-if="Math.abs(pt.r - 1.0) > 0.001">({{ formatOffsetPercent(pt.r) }})</span>
                </td>
                <td :class="Math.abs(pt.g - 1.0) > 0.001 ? 'text-success fw-bold' : 'text-muted'">
                  {{ formatMultiplier(pt.g) }}x
                  <span class="small text-muted" v-if="Math.abs(pt.g - 1.0) > 0.001">({{ formatOffsetPercent(pt.g) }})</span>
                </td>
                <td :class="Math.abs(pt.b - 1.0) > 0.001 ? 'text-info fw-bold' : 'text-muted'">
                  {{ formatMultiplier(pt.b) }}x
                  <span class="small text-muted" v-if="Math.abs(pt.b - 1.0) > 0.001">({{ formatOffsetPercent(pt.b) }})</span>
                </td>
                <td class="text-end">
                  <button
                    class="btn btn-xs btn-outline-danger py-0 px-2"
                    type="button"
                    @click.stop="removePoint(pt.nits)"
                    :disabled="points.length <= 2"
                    title="Delete checkpoint"
                  >
                    <i class="fa-solid fa-trash"></i>
                  </button>
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

      <!-- Action Footer -->
      <div class="d-flex justify-content-between align-items-center pt-2 border-top">
        <button class="btn btn-outline-danger btn-sm" type="button" @click="resetAllPoints">
          <i class="fa-solid fa-rotate-left me-1"></i>Reset All Points to 1.000x (Neutral)
        </button>
        <button class="btn btn-primary btn-sm px-4" type="button" @click="saveProfile">
          <i class="fa-solid fa-floppy-disk me-1"></i>Save Calibration Profile
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.btn-xs {
  font-size: 0.75rem;
}
</style>
