<script setup>
import { ref, computed, onMounted, onUnmounted, inject } from 'vue'

const $t = inject('i18n').t

const props = defineProps([
  'platform',
  'config'
])

// Number of chart bands (4 to 15). Every band is half as bright as the one above.
const bandCount = ref(12)

const isToolOpen = ref(false)
const saveSuccess = ref(false)
const saveError = ref(false)

const chartActive = ref(false)
const peakNits = ref(1000)
const linePx = ref(2)
const endStrips = ref(true)

const eotfEnabled = ref(false)
const shadowGamma = ref(1.25)
const shadowKnee = ref(10000)
const shadowSlope = ref(1.0)

const rangeStops = ref(1)

// One value per chart band, in stops: the reading of that band on the chart.
// It is the error of one halving step (band reference -> band target), so it is relative to the
// band above. The gain sent to the server is the running total from the top band down.
// Index 0 is the top band.
const bandStops = ref(Array(12).fill(0))

const kneePresets = [10, 25, 50, 100, 200, 500, 1000, 10000]

const formatNits = (v) => {
  if (v >= 100) return v.toFixed(0)
  if (v >= 10) return v.toFixed(1)
  if (v >= 1) return v.toFixed(2)
  return v.toFixed(3)
}

// Band k: reference lines at peak / 2^k, solid strip centred on half of that
const bands = computed(() => {
  const list = []
  for (let k = 0; k < bandCount.value; k++) {
    const reference = peakNits.value / Math.pow(2, k)
    list.push({ index: k, reference, target: reference / 2 })
  }
  return list
})

const stopsToGain = (stops) => Math.pow(2, stops)

const formatStops = (stops) => (stops >= 0 ? '+' : '') + Number(stops).toFixed(2)

// Running total of the band readings from the top band down, in stops
const totalStops = computed(() => {
  const totals = []
  let sum = 0
  for (let k = 0; k < bandCount.value; k++) {
    sum += Number(bandStops.value[k]) || 0
    totals.push(sum)
  }
  return totals
})

// The peak itself is the anchor (gain 1). The eye cannot judge absolute brightness.
const buildPoints = () => {
  const points = [{ nits: peakNits.value, gain: 1.0 }]
  bands.value.forEach((band) => {
    points.push({ nits: band.target, gain: stopsToGain(totalStops.value[band.index]) })
  })
  return points
}

const loadPoints = (points) => {
  if (!Array.isArray(points) || points.length < 2) {
    return
  }
  const sorted = [...points].sort((a, b) => b.nits - a.nits)
  peakNits.value = Math.round(sorted[0].nits)
  const stops = Array(bandCount.value).fill(0)
  let previous = 0
  for (let k = 0; k < bandCount.value; k++) {
    const pt = sorted[k + 1]
    if (pt && pt.gain > 0) {
      const total = Math.log2(pt.gain)
      stops[k] = Math.round((total - previous) * 1000) / 1000
      previous = total
    }
  }
  bandStops.value = stops
}

const syncConfig = () => {
  props.config.hdr_shadow_gamma = shadowGamma.value
  props.config.hdr_shadow_knee = shadowKnee.value
  props.config.hdr_shadow_slope = shadowSlope.value
  props.config.hdr_eotf_enabled = eotfEnabled.value
  props.config.hdr_eotf_data = JSON.stringify(buildPoints())
}

const buildPayload = () => ({
  chart_active: chartActive.value,
  chart_peak_nits: peakNits.value,
  chart_line_px: linePx.value,
  chart_end_strips: endStrips.value,
  chart_range_stops: rangeStops.value,
  chart_bands: bandCount.value,
  eotf_enabled: eotfEnabled.value,
  eotf_points: buildPoints(),
  shadow_gamma: shadowGamma.value,
  shadow_knee: shadowKnee.value,
  shadow_slope: shadowSlope.value
})

const fetchState = async () => {
  try {
    const res = await fetch('/api/hdr-eotf')
    if (!res.ok) return
    const data = await res.json()
    if (typeof data.chart_bands === 'number') {
      bandCount.value = data.chart_bands
    }
    chartActive.value = !!data.chart_active
    peakNits.value = data.chart_peak_nits ?? 1000
    linePx.value = data.chart_line_px ?? 2
    endStrips.value = data.chart_end_strips !== false
    rangeStops.value = data.chart_range_stops ?? 1
    eotfEnabled.value = !!data.eotf_enabled
    shadowGamma.value = data.shadow_gamma ?? 1.25
    shadowKnee.value = data.shadow_knee ?? 10000
    shadowSlope.value = data.shadow_slope ?? 1.0
    bandStops.value = Array(bandCount.value).fill(0)
    loadPoints(data.eotf_points)
    if (chartActive.value) {
      startPolling()
    }
  } catch (err) {
    console.warn('Failed to load EOTF state:', err)
  }
}

// The chart can be hidden with Esc on the host or the client, so watch for that
let pollTimer = null

const stopPolling = () => {
  if (pollTimer) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

const startPolling = () => {
  stopPolling()
  pollTimer = setInterval(async () => {
    if (!chartActive.value) {
      stopPolling()
      return
    }
    try {
      const res = await fetch('/api/hdr-eotf')
      if (res.ok) {
        const data = await res.json()
        if (data.chart_active === false) {
          chartActive.value = false
          stopPolling()
        }
      }
    } catch (_) {}
  }, 1000)
}

onMounted(fetchState)
onUnmounted(stopPolling)

let isUpdating = false
let updatePending = false

const sendLiveUpdate = async () => {
  syncConfig()
  if (isUpdating) {
    updatePending = true
    return
  }
  isUpdating = true
  try {
    do {
      updatePending = false
      await fetch('/api/hdr-eotf', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(buildPayload())
      })
    } while (updatePending)
  } catch (err) {
    console.error('Failed to send live EOTF update:', err)
  } finally {
    isUpdating = false
  }
}

const toggleChart = () => {
  chartActive.value = !chartActive.value
  if (chartActive.value) {
    startPolling()
  } else {
    stopPolling()
  }
  sendLiveUpdate()
}

const onPeakChange = () => {
  let v = parseFloat(peakNits.value)
  if (isNaN(v)) v = 1000
  peakNits.value = Math.min(10000, Math.max(10, v))
  sendLiveUpdate()
}

// Keep the values of the bands that stay, new bands start at 0
const onBandCountChange = () => {
  const stops = Array(bandCount.value).fill(0)
  for (let k = 0; k < bandCount.value; k++) {
    stops[k] = Number(bandStops.value[k]) || 0
  }
  bandStops.value = stops
  sendLiveUpdate()
}

const onGammaChange = () => {
  let v = parseFloat(shadowGamma.value)
  if (isNaN(v)) v = 1.0
  shadowGamma.value = Math.min(2.5, Math.max(0.8, v))
  sendLiveUpdate()
}

const onSlopeChange = () => {
  let v = parseFloat(shadowSlope.value)
  if (isNaN(v)) v = 1.0
  shadowSlope.value = Math.min(4, Math.max(0.25, v))
  sendLiveUpdate()
}

const onKneeChange = () => {
  let v = parseFloat(shadowKnee.value)
  if (isNaN(v)) v = 10000
  shadowKnee.value = Math.min(10000, Math.max(1, v))
  sendLiveUpdate()
}

const setKnee = (nits) => {
  shadowKnee.value = nits
  sendLiveUpdate()
}

const resetBand = (index) => {
  bandStops.value[index] = 0
  sendLiveUpdate()
}

const resetAllBands = () => {
  bandStops.value = Array(bandCount.value).fill(0)
  sendLiveUpdate()
}

// ---- Final adjustment graph ----
// Same maths as ApplyHDRCalibration in common.hlsl (band gains, then shadow slope, then shadow gamma).
// The colour calibration multipliers are not part of it.
const PQ_M1 = 2610 / 4096 / 4
const PQ_M2 = (2523 / 4096) * 128
const PQ_C1 = 3424 / 4096
const PQ_C2 = (2413 / 4096) * 32
const PQ_C3 = (2392 / 4096) * 32

const nitsToPq = (L) => {
  const lp = Math.pow(Math.min(Math.max(L / 10000, 0), 1), PQ_M1)
  return Math.pow((PQ_C1 + PQ_C2 * lp) / (1 + PQ_C3 * lp), PQ_M2)
}

const pqToNits = (P) => {
  const pp = Math.pow(Math.min(Math.max(P, 0), 1), 1 / PQ_M2)
  const num = Math.max(pp - PQ_C1, 0)
  const den = Math.max(PQ_C2 - PQ_C3 * pp, 0.000001)
  return 10000 * Math.pow(num / den, 1 / PQ_M1)
}

// Band gain points sorted from dark to bright, positions in PQ like the shader
const gainPoints = computed(() =>
  buildPoints()
    .map((p) => ({ pq: nitsToPq(p.nits), gain: Math.min(1024, Math.max(1 / 1024, p.gain)) }))
    .sort((a, b) => a.pq - b.pq)
)

const lookupGain = (P) => {
  const pts = gainPoints.value
  if (pts.length === 0) return 1
  if (P <= pts[0].pq) return pts[0].gain
  if (P >= pts[pts.length - 1].pq) return pts[pts.length - 1].gain
  for (let i = 0; i < pts.length - 1; i++) {
    if (P >= pts[i].pq && P < pts[i + 1].pq) {
      const span = pts[i + 1].pq - pts[i].pq
      const t = span > 0.0001 ? Math.min(1, Math.max(0, (P - pts[i].pq) / span)) : 0
      return pts[i].gain + (pts[i + 1].gain - pts[i].gain) * t
    }
  }
  return 1
}

const adjustedNits = (L) => {
  let out = L
  if (eotfEnabled.value) {
    out *= lookupGain(nitsToPq(out))
  }
  const gamma = Number(shadowGamma.value) || 1
  const slope = Number(shadowSlope.value) || 1
  const hasGamma = Math.abs(gamma - 1) > 0.0005
  const hasSlope = Math.abs(slope - 1) > 0.0005
  if (hasGamma || hasSlope) {
    const kneeNits = Math.min(10000, Math.max(1, Number(shadowKnee.value) || 10000))
    const kneePq = nitsToPq(kneeNits)
    if (out > 0.000001 && nitsToPq(out) < kneePq) {
      if (hasSlope) {
        out = kneeNits * Math.pow(out / kneeNits, slope)
      }
      if (hasGamma) {
        out = pqToNits(kneePq * Math.pow(nitsToPq(out) / kneePq, gamma))
      }
    }
  }
  return out
}

const changeStops = (L) => {
  const out = Math.max(adjustedNits(L), 1e-12)
  return Math.min(20, Math.max(-20, Math.log2(out / L)))
}

const GRAPH = { w: 640, h: 230, left: 46, right: 14, top: 14, bottom: 30 }
const graphHoverNits = ref(null)
// 'curve' = output against input (no change is the diagonal), 'change' = change in stops (no change is flat)
const graphMode = ref('curve')
const graphDragIndex = ref(null)
const graphDragRange = ref(null)

const graph = computed(() => {
  const plotW = GRAPH.w - GRAPH.left - GRAPH.right
  const plotH = GRAPH.h - GRAPH.top - GRAPH.bottom
  const lowestTarget = peakNits.value / Math.pow(2, bandCount.value)
  const xMinLog = Math.floor(Math.log10(Math.min(0.01, lowestTarget)))
  const xMaxLog = 4

  const samples = []
  const count = 240
  let maxAbs = 0
  for (let i = 0; i <= count; i++) {
    const nits = Math.pow(10, xMinLog + ((xMaxLog - xMinLog) * i) / count)
    const stops = changeStops(nits)
    maxAbs = Math.max(maxAbs, Math.abs(stops))
    samples.push({ nits, stops })
  }
  // While a dot is dragged the scale is frozen, so the graph does not jump under the mouse
  const yRange = graphDragRange.value || [0.5, 1, 2, 4, 6, 10, 20].find((r) => r >= maxAbs) || 20

  const curveMode = graphMode.value === 'curve'
  const LOG2 = Math.log10(2)

  const x = (nits) => GRAPH.left + ((Math.log10(nits) - xMinLog) / (xMaxLog - xMinLog)) * plotW
  // Curve mode: up/down is output brightness (same scale as input, so "no change" is the diagonal).
  // Change mode: up/down is the change in stops (so "no change" is the flat 0 line).
  const yOutLog = (outLog) =>
    GRAPH.top + ((xMaxLog - Math.min(xMaxLog, Math.max(xMinLog, outLog))) / (xMaxLog - xMinLog)) * plotH
  const yStops = (stops) => GRAPH.top + ((yRange - stops) / (2 * yRange)) * plotH
  const py = (nits, stops) => (curveMode ? yOutLog(Math.log10(nits) + stops * LOG2) : yStops(stops))

  const path = samples.map((s, i) => (i === 0 ? 'M' : 'L') + x(s.nits).toFixed(1) + ' ' + py(s.nits, s.stops).toFixed(1)).join(' ')

  const xTicks = []
  const decadeTicks = []
  for (let d = xMinLog; d <= xMaxLog; d++) {
    const nits = Math.pow(10, d)
    const label = d < 0 ? nits.toFixed(-d) : String(nits)
    xTicks.push({ x: x(nits), label })
    decadeTicks.push({ y: yOutLog(d), zero: false, label })
  }
  const yTicks = curveMode
    ? decadeTicks
    : [yRange, yRange / 2, 0, -yRange / 2, -yRange].map((s) => ({
        y: yStops(s),
        zero: s === 0,
        label: (s > 0 ? '+' : '') + s
      }))

  // The "no change" line of curve mode: output = input
  const diagonal = curveMode
    ? { x1: x(Math.pow(10, xMinLog)), y1: yOutLog(xMinLog), x2: x(Math.pow(10, xMaxLog)), y2: yOutLog(xMaxLog) }
    : null

  const dots = eotfEnabled.value
    ? bands.value.map((band) => ({ x: x(band.target), y: py(band.target, changeStops(band.target)) }))
    : []

  const curveOn = Math.abs(shadowGamma.value - 1) > 0.0005 || Math.abs(shadowSlope.value - 1) > 0.0005
  const kneeX = curveOn && shadowKnee.value < 10000 ? x(Math.max(1, shadowKnee.value)) : null

  const fromX = (vx) => Math.pow(10, xMinLog + ((vx - GRAPH.left) / plotW) * (xMaxLog - xMinLog))

  // Mouse height -> wanted change in stops for the level `nits`
  const fromY = (vy, nits) => {
    if (curveMode) {
      const outLog = xMaxLog - ((vy - GRAPH.top) / plotH) * (xMaxLog - xMinLog)
      return (outLog - Math.log10(nits)) / LOG2
    }
    return yRange - ((vy - GRAPH.top) / plotH) * 2 * yRange
  }

  return { path, xTicks, yTicks, dots, kneeX, diagonal, curveMode, x, py, fromX, fromY, yRange, plotBottom: GRAPH.top + plotH, plotRight: GRAPH.left + plotW }
})

const graphHover = computed(() => {
  if (graphHoverNits.value === null) return null
  const nits = graphHoverNits.value
  const stops = changeStops(nits)
  return { nits, out: adjustedNits(nits), stops, x: graph.value.x(nits), y: graph.value.py(nits, stops) }
})

const clampBandStop = (v) => Math.round(Math.min(5, Math.max(-5, v)) * 100) / 100

// Dragging a dot moves only that level: its own band value changes, and the band below
// gets the opposite change so the levels below stay where they are.
const dragDotTo = (index, wantedStops) => {
  const band = bands.value[index]
  if (!band) return
  let delta = wantedStops - changeStops(band.target)
  // Below the knee the shadow slope multiplies the band fix, so undo that
  const slope = Number(shadowSlope.value) || 1
  if (Math.abs(slope - 1) > 0.0005 && band.target < shadowKnee.value) {
    delta /= slope
  }
  const before = Number(bandStops.value[index]) || 0
  const after = clampBandStop(before + delta)
  const applied = after - before
  if (applied === 0) return
  bandStops.value[index] = after
  if (index + 1 < bandCount.value) {
    bandStops.value[index + 1] = clampBandStop((Number(bandStops.value[index + 1]) || 0) - applied)
  }
  sendLiveUpdate()
}

const onDotDown = (index, event) => {
  if (!eotfEnabled.value) return
  graphDragRange.value = graph.value.yRange
  graphDragIndex.value = index
  event.currentTarget.ownerSVGElement.setPointerCapture(event.pointerId)
  event.preventDefault()
}

const onGraphUp = () => {
  graphDragIndex.value = null
  graphDragRange.value = null
}

const onGraphMove = (event) => {
  const rect = event.currentTarget.getBoundingClientRect()
  if (rect.width <= 0 || rect.height <= 0) return
  if (graphDragIndex.value !== null) {
    const vy = ((event.clientY - rect.top) / rect.height) * GRAPH.h
    const clampedY = Math.min(graph.value.plotBottom, Math.max(GRAPH.top, vy))
    const target = bands.value[graphDragIndex.value].target
    dragDotTo(graphDragIndex.value, graph.value.fromY(clampedY, target))
    graphHoverNits.value = target
    return
  }
  const vx = ((event.clientX - rect.left) / rect.width) * GRAPH.w
  const clamped = Math.min(graph.value.plotRight, Math.max(GRAPH.left, vx))
  graphHoverNits.value = graph.value.fromX(clamped)
}

const onGraphLeave = () => {
  if (graphDragIndex.value === null) {
    graphHoverNits.value = null
  }
}

const saveProfile = async () => {
  saveSuccess.value = false
  saveError.value = false
  try {
    syncConfig()
    const payload = buildPayload()
    delete payload.chart_active
    const res = await fetch('/api/hdr-eotf/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    })
    if (res.ok) {
      saveSuccess.value = true
      setTimeout(() => {
        saveSuccess.value = false
      }, 4000)
    } else {
      saveError.value = true
    }
  } catch (err) {
    saveError.value = true
    console.error('Failed to save EOTF correction:', err)
  }
}
</script>

<template>
  <div class="mb-4 p-3 border rounded bg-body-tertiary">
    <div class="d-flex align-items-center justify-content-between mb-2">
      <div>
        <h5 class="mb-1 text-primary">
          <i class="fa-solid fa-chart-line me-2"></i>{{ $t('config.hdr_eotf') }}
        </h5>
        <div class="form-text mt-0">{{ $t('config.hdr_eotf_desc') }}</div>
      </div>
    </div>

    <div class="mt-2">
      <button class="btn btn-outline-primary btn-sm" type="button" @click="isToolOpen = !isToolOpen">
        <i class="fa-solid" :class="isToolOpen ? 'fa-chevron-up' : 'fa-sliders'"></i>
        {{ isToolOpen ? 'Close EOTF Check' : 'Open EOTF Check' }}
      </button>
    </div>

    <div v-if="isToolOpen" class="mt-3 p-3 border rounded bg-body">
      <!-- Chart controls -->
      <div class="card mb-3 shadow-sm">
        <div class="card-header bg-primary bg-opacity-10 py-2 d-flex justify-content-between align-items-center">
          <strong class="text-primary"><i class="fa-solid fa-bars-staggered me-2"></i>In-Stream EOTF Chart</strong>
          <div class="d-flex align-items-center gap-1">
            <span class="badge" :class="chartActive ? 'bg-success' : 'bg-secondary'">
              {{ chartActive ? 'Chart Active on Stream' : 'Chart Hidden (Showing Stream)' }}
            </span>
            <span v-if="chartActive" class="badge bg-light text-dark border">
              <i class="fa-solid fa-keyboard me-1"></i>Esc hides
            </span>
          </div>
        </div>
        <div class="card-body">
          <div class="d-flex flex-wrap gap-3 align-items-end mb-3">
            <button class="btn btn-sm" :class="chartActive ? 'btn-danger' : 'btn-success'" type="button" @click="toggleChart">
              <i class="fa-solid" :class="chartActive ? 'fa-stop me-1' : 'fa-play me-1'"></i>
              {{ chartActive ? 'Hide Chart (Resume Stream)' : 'Show Chart on Stream' }}
            </button>

            <div>
              <label for="eotf_peak_nits" class="form-label small mb-1">Top band (nits)</label>
              <input
                type="number"
                class="form-control form-control-sm"
                style="width: 110px;"
                id="eotf_peak_nits"
                min="10"
                max="10000"
                step="10"
                v-model.number="peakNits"
                @change="onPeakChange"
              />
            </div>

            <div>
              <label for="eotf_line_px" class="form-label small mb-1">Line thickness</label>
              <select id="eotf_line_px" class="form-select form-select-sm" v-model.number="linePx" @change="sendLiveUpdate">
                <option :value="1">1 px</option>
                <option :value="2">2 px</option>
                <option :value="4">4 px</option>
              </select>
            </div>

            <div>
              <label for="eotf_bands" class="form-label small mb-1">Bands (down to {{ formatNits(peakNits / Math.pow(2, bandCount)) }} nits)</label>
              <select id="eotf_bands" class="form-select form-select-sm" v-model.number="bandCount" @change="onBandCountChange">
                <option v-for="n in [8, 10, 12, 14, 15]" :key="n" :value="n">{{ n }}</option>
              </select>
            </div>

            <div>
              <label for="eotf_range_stops" class="form-label small mb-1">Strip range</label>
              <select id="eotf_range_stops" class="form-select form-select-sm" v-model.number="rangeStops" @change="sendLiveUpdate">
                <option :value="1">&plusmn;1 stop (coarse)</option>
                <option :value="0.5">&plusmn;0.5 stop</option>
                <option :value="0.25">&plusmn;0.25 stop (fine)</option>
              </select>
            </div>

            <div class="form-check mb-1">
              <input class="form-check-input" type="checkbox" id="eotf_end_strips" v-model="endStrips" @change="sendLiveUpdate" />
              <label class="form-check-label small" for="eotf_end_strips">Highlight and near-black strips</label>
            </div>
          </div>

          <div class="form-text">
            <strong>How to read it:</strong> every band has fine lines and a solid strip. Look from far enough that the lines
            melt into grey. The strip disappears where it matches the lines. At the centre mark the brightness curve is correct.
            Left of centre means this level is shown too bright, right of centre means too dark.
            The ruler on top shows the error in stops.
            Each band shows the error of one halving step only (from the level on its right to the level on its left),
            so small errors add up down the chart. Use a smaller strip range for a finer reading.
          </div>
          <div class="form-text">
            <strong>Needs:</strong> an HDR stream at the display's own resolution with no scaling, and a high bitrate.
            Set the top band below the level where the top strip stops showing steps (the display clips there).
            The bottom strip shows 0.005 to 2 nits for checking black crush.
          </div>
        </div>
      </div>

      <!-- Shadow curve -->
      <div class="card mb-3 shadow-sm">
        <div class="card-header bg-body-secondary py-2">
          <strong class="small text-uppercase"><i class="fa-solid fa-moon me-2"></i>{{ $t('config.hdr_shadow_gamma') }}</strong>
        </div>
        <div class="card-body">
          <div class="row g-3">
            <div class="col-12 col-md-6">
              <label for="hdr_shadow_gamma" class="form-label mb-1">Gamma: <span class="font-monospace">{{ Number(shadowGamma).toFixed(2) }}</span></label>
              <div class="d-flex align-items-center gap-2">
                <input type="range" class="form-range flex-grow-1" min="0.8" max="2.5" step="0.01" v-model.number="shadowGamma" @input="onGammaChange" />
                <input
                  type="number"
                  class="form-control form-control-sm text-end font-monospace"
                  style="width: 85px;"
                  id="hdr_shadow_gamma"
                  min="0.8"
                  max="2.5"
                  step="0.01"
                  v-model.number="shadowGamma"
                  @change="onGammaChange"
                />
                <button class="btn btn-outline-secondary btn-sm py-0 px-2" type="button" title="Turn off" @click="shadowGamma = 1.0; onGammaChange()">1.0</button>
              </div>
              <div class="form-text">{{ $t('config.hdr_shadow_gamma_desc') }}</div>
            </div>
            <div class="col-12 col-md-6">
              <label for="hdr_shadow_slope" class="form-label mb-1">{{ $t('config.hdr_shadow_slope') }}: <span class="font-monospace">{{ Number(shadowSlope).toFixed(2) }}</span></label>
              <div class="d-flex align-items-center gap-2">
                <input type="range" class="form-range flex-grow-1" min="0.25" max="4" step="0.01" v-model.number="shadowSlope" @input="onSlopeChange" />
                <input
                  type="number"
                  class="form-control form-control-sm text-end font-monospace"
                  style="width: 85px;"
                  id="hdr_shadow_slope"
                  min="0.25"
                  max="4"
                  step="0.01"
                  v-model.number="shadowSlope"
                  @change="onSlopeChange"
                />
                <button class="btn btn-outline-secondary btn-sm py-0 px-2" type="button" title="Turn off" @click="shadowSlope = 1.0; onSlopeChange()">1.0</button>
              </div>
              <div class="form-text">{{ $t('config.hdr_shadow_slope_desc') }}</div>
            </div>
            <div class="col-12 col-md-6">
              <label for="hdr_shadow_knee" class="form-label mb-1">{{ $t('config.hdr_shadow_knee') }}</label>
              <div class="d-flex flex-wrap align-items-center gap-1">
                <input
                  type="number"
                  class="form-control form-control-sm text-end font-monospace"
                  style="width: 100px;"
                  id="hdr_shadow_knee"
                  min="1"
                  max="10000"
                  step="1"
                  v-model.number="shadowKnee"
                  @change="onKneeChange"
                />
                <button
                  v-for="nits in kneePresets"
                  :key="nits"
                  type="button"
                  class="btn btn-sm py-0 px-2"
                  :class="Math.abs(shadowKnee - nits) < 0.01 ? 'btn-primary' : 'btn-outline-secondary'"
                  @click="setKnee(nits)"
                >
                  {{ nits === 10000 ? 'All' : nits }}
                </button>
              </div>
              <div class="form-text">{{ $t('config.hdr_shadow_knee_desc') }}</div>
            </div>
          </div>
        </div>
      </div>

      <!-- Per-band gains -->
      <div class="card mb-3 shadow-sm">
        <div class="card-header bg-body-secondary py-2 d-flex justify-content-between align-items-center">
          <strong class="small text-uppercase"><i class="fa-solid fa-list-ol me-2"></i>Brightness Fix per Band</strong>
          <div class="form-check form-switch mb-0">
            <input class="form-check-input" type="checkbox" role="switch" id="hdr_eotf_enabled_toggle" v-model="eotfEnabled" @change="sendLiveUpdate" />
            <label class="form-check-label small" for="hdr_eotf_enabled_toggle">Apply to stream</label>
          </div>
        </div>
        <div class="card-body">
          <div class="form-text mt-0 mb-2">
            For each band, read on the ruler where the strip disappears and set that band's slider to the same value.
            The strip then moves to the centre. A band's value does not move the bands below it on the chart,
            but it does change their brightness: the fix sent to the stream is the running total from the top band down.
            The top level ({{ formatNits(peakNits) }} nits) is the fixed anchor.
          </div>
          <div v-for="band in bands" :key="band.index" class="d-flex align-items-center gap-2 mb-1">
            <div class="font-monospace small text-end" style="width: 95px;">{{ formatNits(band.target) }} nits</div>
            <input
              type="range"
              class="form-range flex-grow-1"
              min="-5"
              max="5"
              step="0.01"
              v-model.number="bandStops[band.index]"
              :disabled="!eotfEnabled"
              @input="sendLiveUpdate"
            />
            <input
              type="number"
              class="form-control form-control-sm text-end font-monospace px-1"
              style="width: 80px;"
              min="-5"
              max="5"
              step="0.01"
              v-model.number="bandStops[band.index]"
              :disabled="!eotfEnabled"
              @input="sendLiveUpdate"
            />
            <div class="font-monospace small text-muted" style="width: 150px;" title="Running total sent to the stream">
              total {{ formatStops(totalStops[band.index] || 0) }} = {{ stopsToGain(totalStops[band.index] || 0).toFixed(3) }}x
            </div>
            <button class="btn btn-outline-secondary btn-sm py-0 px-2" type="button" title="Reset band" @click="resetBand(band.index)">0</button>
          </div>
        </div>
      </div>

      <!-- Final adjustment graph -->
      <div class="card mb-3 shadow-sm">
        <div class="card-header bg-body-secondary py-2 d-flex justify-content-between align-items-center">
          <strong class="small text-uppercase"><i class="fa-solid fa-chart-line me-2"></i>Final Adjustment</strong>
          <div class="btn-group btn-group-sm" role="group" aria-label="Graph view">
            <button type="button" class="btn py-0" :class="graphMode === 'curve' ? 'btn-primary' : 'btn-outline-secondary'" @click="graphMode = 'curve'">Curve (in / out)</button>
            <button type="button" class="btn py-0" :class="graphMode === 'change' ? 'btn-primary' : 'btn-outline-secondary'" @click="graphMode = 'change'">Change (zoomed)</button>
          </div>
        </div>
        <div class="card-body">
          <div class="form-text mt-0 mb-2">
            Everything above added together: band fixes, shadow slope and shadow gamma.
            <template v-if="graphMode === 'curve'">
              The line shows the brightness sent to the display for each input brightness.
              On the dashed diagonal nothing changes. Above it is brighter, below it is darker.
              Small changes are hard to see here: use "Change (zoomed)" for those.
            </template>
            <template v-else>
              The line shows how much each brightness level is changed, in stops.
              On the 0 line nothing changes. Above it is brighter, below it is darker. One stop down is half the brightness.
            </template>
          </div>
          <svg
            :viewBox="`0 0 ${GRAPH.w} ${GRAPH.h}`"
            style="width: 100%; height: auto; max-height: 320px; display: block; touch-action: none;"
            role="img"
            aria-label="Final brightness change in stops for each input brightness level"
            @pointermove="onGraphMove"
            @pointerleave="onGraphLeave"
            @pointerup="onGraphUp"
            @pointercancel="onGraphUp"
          >
            <g v-for="tick in graph.yTicks" :key="'y' + tick.label">
              <line
                :x1="GRAPH.left"
                :x2="graph.plotRight"
                :y1="tick.y"
                :y2="tick.y"
                :stroke="tick.zero ? 'var(--bs-secondary-color)' : 'var(--bs-border-color)'"
                stroke-width="1"
              />
              <text :x="GRAPH.left - 6" :y="tick.y + 4" text-anchor="end" font-size="11" fill="var(--bs-secondary-color)">{{ tick.label }}</text>
            </g>
            <g v-for="tick in graph.xTicks" :key="'x' + tick.label">
              <line :x1="tick.x" :x2="tick.x" :y1="graph.plotBottom" :y2="graph.plotBottom + 4" stroke="var(--bs-secondary-color)" stroke-width="1" />
              <text :x="tick.x" :y="graph.plotBottom + 16" text-anchor="middle" font-size="11" fill="var(--bs-secondary-color)">{{ tick.label }}</text>
            </g>
            <text :x="graph.plotRight" :y="GRAPH.h - 2" text-anchor="end" font-size="11" fill="var(--bs-secondary-color)">input brightness (nits)</text>
            <text :x="GRAPH.left + 4" :y="GRAPH.top + 10" font-size="11" fill="var(--bs-secondary-color)">{{ graph.curveMode ? 'output brightness (nits)' : 'change (stops)' }}</text>
            <line
              v-if="graph.diagonal"
              :x1="graph.diagonal.x1"
              :y1="graph.diagonal.y1"
              :x2="graph.diagonal.x2"
              :y2="graph.diagonal.y2"
              stroke="var(--bs-secondary-color)"
              stroke-width="1"
              stroke-dasharray="2 4"
            />

            <g v-if="graph.kneeX !== null">
              <line :x1="graph.kneeX" :x2="graph.kneeX" :y1="GRAPH.top" :y2="graph.plotBottom" stroke="var(--bs-secondary-color)" stroke-width="1" stroke-dasharray="4 4" />
              <text :x="graph.kneeX - 4" :y="graph.plotBottom - 4" text-anchor="end" font-size="11" fill="var(--bs-secondary-color)">knee</text>
            </g>

            <path :d="graph.path" fill="none" stroke="var(--bs-primary)" stroke-width="2" stroke-linejoin="round" stroke-linecap="round" />
            <circle v-for="(dot, i) in graph.dots" :key="'d' + i" :cx="dot.x" :cy="dot.y" r="4" fill="var(--bs-primary)" stroke="var(--bs-body-bg)" stroke-width="2" />

            <g v-if="graphHover" style="pointer-events: none;">
              <line :x1="graphHover.x" :x2="graphHover.x" :y1="GRAPH.top" :y2="graph.plotBottom" stroke="var(--bs-secondary-color)" stroke-width="1" />
              <circle :cx="graphHover.x" :cy="graphHover.y" r="5" fill="var(--bs-primary)" stroke="var(--bs-body-bg)" stroke-width="2" />
            </g>

            <!-- Large invisible grab areas over the band dots -->
            <circle
              v-for="(dot, i) in graph.dots"
              :key="'g' + i"
              :cx="dot.x"
              :cy="dot.y"
              r="11"
              fill="transparent"
              style="cursor: ns-resize;"
              @pointerdown="onDotDown(i, $event)"
            />
          </svg>
          <div class="small font-monospace mt-1" style="min-height: 1.5em;">
            <template v-if="graphHover">
              {{ formatNits(graphHover.nits) }} nits in &rarr; {{ formatNits(graphHover.out) }} nits out
              ({{ formatStops(graphHover.stops) }} stops, {{ stopsToGain(graphHover.stops).toFixed(3) }}x)
            </template>
            <span v-else class="text-muted">Move the mouse over the graph to read a level. Dots are the chart bands: drag one up or down to change that level.</span>
          </div>
        </div>
      </div>

      <div class="d-flex justify-content-between align-items-center pt-2 border-top">
        <button class="btn btn-outline-danger btn-sm" type="button" @click="resetAllBands">
          <i class="fa-solid fa-rotate-left me-1"></i>Reset All Bands
        </button>
        <div class="d-flex align-items-center gap-2">
          <span v-if="saveSuccess" class="badge bg-success py-2 px-3"><i class="fa-solid fa-floppy-disk me-1"></i>Saved to config!</span>
          <span v-if="saveError" class="badge bg-danger py-2 px-3">Save failed</span>
          <button class="btn btn-primary btn-sm px-4" type="button" @click="saveProfile">
            <i class="fa-solid fa-floppy-disk me-1"></i>Save EOTF Correction
          </button>
        </div>
      </div>
    </div>
  </div>
</template>
