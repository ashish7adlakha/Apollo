<script setup>
import { ref, onMounted, computed, inject } from 'vue'
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
  { nits: 0.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 0.5, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 1.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 2.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 5.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 10.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 25.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 50.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 100.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 200.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 400.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 800.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 1000.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 1200.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 2000.0, r: 0.0, g: 0.0, b: 0.0 },
  { nits: 4000.0, r: 0.0, g: 0.0, b: 0.0 }
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

const fetchCalibration = async () => {
  try {
    isLoading.value = true
    const res = await fetch('/api/hdr-calibration')
    if (res.ok) {
      const data = await res.json()
      enabled.value = !!data.enabled
      props.config.hdr_calibration_enabled = enabled.value
      testPatternActive.value = !!data.test_pattern_active
      testPatternNits.value = data.test_pattern_nits ?? 10.0
      testPatternFullscreen.value = !!data.test_pattern_fullscreen
      if (Array.isArray(data.points) && data.points.length > 0) {
        points.value = data.points
      }
    }
  } catch (err) {
    console.warn('Failed to load HDR calibration:', err)
  } finally {
    isLoading.value = false
  }
}

onMounted(() => {
  fetchCalibration()
})

const sendLiveUpdate = async () => {
  try {
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
  } catch (err) {
    console.error('Failed to send live calibration update:', err)
  }
}

const toggleEnabled = () => {
  props.config.hdr_calibration_enabled = enabled.value
  sendLiveUpdate()
}

const toggleTestPattern = () => {
  testPatternActive.value = !testPatternActive.value
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

const resetActivePoint = () => {
  if (activePoint.value) {
    activePoint.value.r = 0.0
    activePoint.value.g = 0.0
    activePoint.value.b = 0.0
    sendLiveUpdate()
  }
}

const resetAllPoints = () => {
  points.value.forEach(p => {
    p.r = 0.0
    p.g = 0.0
    p.b = 0.0
  })
  sendLiveUpdate()
}

const addCustomPoint = () => {
  const nitsVal = parseFloat(newCustomNits.value)
  if (isNaN(nitsVal) || nitsVal < 0) return
  if (!points.value.some(p => Math.abs(p.nits - nitsVal) < 0.01)) {
    points.value.push({ nits: nitsVal, r: 0.0, g: 0.0, b: 0.0 })
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
          <strong>Real-Time Stream Calibration:</strong> Adjustments apply immediately to your active HDR video stream.
        </div>
        <div v-if="saveSuccess" class="badge bg-success py-2 px-3">
          <i class="fa-solid fa-floppy-disk me-1"></i> Saved to config!
        </div>
      </div>

      <!-- Test Pattern Controls Bar -->
      <div class="card mb-3 shadow-sm">
        <div class="card-header bg-primary bg-opacity-10 py-2 d-flex justify-content-between align-items-center">
          <strong class="text-primary"><i class="fa-solid fa-bullseye me-2"></i>In-Stream Test Pattern Generator</strong>
          <span class="badge" :class="testPatternActive ? 'bg-success' : 'bg-secondary'">
            {{ testPatternActive ? 'Pattern Active on Stream' : 'Pattern Inactive (Showing Stream)' }}
          </span>
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
            <i class="fa-solid fa-rotate-left me-1"></i>Reset Point
          </button>
        </div>
        <div class="card-body">
          <div class="row g-3">
            <!-- Red Slider -->
            <div class="col-12 col-md-4">
              <label class="form-label d-flex justify-content-between text-danger fw-bold">
                <span><i class="fa-solid fa-circle me-1"></i>Red Adjustment:</span>
                <span>{{ activePoint.r > 0 ? '+' : '' }}{{ activePoint.r.toFixed(1) }}%</span>
              </label>
              <input
                type="range"
                class="form-range"
                min="-25.0"
                max="25.0"
                step="0.5"
                v-model.number="activePoint.r"
                @input="onSliderChange"
              />
            </div>

            <!-- Green Slider -->
            <div class="col-12 col-md-4">
              <label class="form-label d-flex justify-content-between text-success fw-bold">
                <span><i class="fa-solid fa-circle me-1"></i>Green Adjustment:</span>
                <span>{{ activePoint.g > 0 ? '+' : '' }}{{ activePoint.g.toFixed(1) }}%</span>
              </label>
              <input
                type="range"
                class="form-range"
                min="-25.0"
                max="25.0"
                step="0.5"
                v-model.number="activePoint.g"
                @input="onSliderChange"
              />
            </div>

            <!-- Blue Slider -->
            <div class="col-12 col-md-4">
              <label class="form-label d-flex justify-content-between text-info fw-bold">
                <span><i class="fa-solid fa-circle me-1"></i>Blue Adjustment:</span>
                <span>{{ activePoint.b > 0 ? '+' : '' }}{{ activePoint.b.toFixed(1) }}%</span>
              </label>
              <input
                type="range"
                class="form-range"
                min="-25.0"
                max="25.0"
                step="0.5"
                v-model.number="activePoint.b"
                @input="onSliderChange"
              />
            </div>
          </div>
          <div class="form-text mt-2">
            <i class="fa-solid fa-info-circle me-1"></i>
            Tip: To eliminate pink/magenta shifts, reduce <strong>Red</strong> and/or slightly raise <strong>Green</strong> until the grey box appears completely neutral on your client monitor.
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
                <td :class="pt.r !== 0 ? 'text-danger fw-bold' : 'text-muted'">
                  {{ pt.r > 0 ? '+' : '' }}{{ pt.r.toFixed(1) }}%
                </td>
                <td :class="pt.g !== 0 ? 'text-success fw-bold' : 'text-muted'">
                  {{ pt.g > 0 ? '+' : '' }}{{ pt.g.toFixed(1) }}%
                </td>
                <td :class="pt.b !== 0 ? 'text-info fw-bold' : 'text-muted'">
                  {{ pt.b > 0 ? '+' : '' }}{{ pt.b.toFixed(1) }}%
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
          <i class="fa-solid fa-trash-arrow-up me-1"></i>Reset All Points to 0%
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
