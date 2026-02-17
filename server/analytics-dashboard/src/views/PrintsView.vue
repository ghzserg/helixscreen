<template>
  <AppLayout>
    <div class="page">
      <div class="page-header">
        <h2>Prints</h2>
        <DateRangePicker v-model="range" />
      </div>

      <div v-if="loading" class="loading">Loading...</div>
      <div v-else-if="error" class="error">{{ error }}</div>
      <template v-else-if="data">
        <div class="chart-section">
          <h3>Success Rate Over Time</h3>
          <LineChart :data="successRateChartData" />
        </div>

        <div class="grid-2col">
          <div class="chart-section">
            <h3>Success by Filament Type</h3>
            <BarChart :data="filamentChartData" />
          </div>
          <MetricCard
            title="Average Print Duration"
            :value="formatDuration(data.avg_duration_sec)"
            subtitle="across all completed prints"
          />
        </div>
      </template>
    </div>
  </AppLayout>
</template>

<script setup lang="ts">
import { ref, watch, computed } from 'vue'
import AppLayout from '@/components/AppLayout.vue'
import DateRangePicker from '@/components/DateRangePicker.vue'
import LineChart from '@/components/LineChart.vue'
import BarChart from '@/components/BarChart.vue'
import MetricCard from '@/components/MetricCard.vue'
import { api } from '@/services/api'
import type { PrintsData } from '@/services/api'

const range = ref('30d')
const data = ref<PrintsData | null>(null)
const loading = ref(true)
const error = ref('')

function formatDuration(seconds: number): string {
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  return h > 0 ? `${h}h ${m}m` : `${m}m`
}

const successRateChartData = computed(() => ({
  labels: data.value?.success_rate_over_time.map(d => d.date) ?? [],
  datasets: [{
    label: 'Success Rate %',
    data: data.value?.success_rate_over_time.map(d => d.rate) ?? [],
    borderColor: '#10b981',
    backgroundColor: 'rgba(16, 185, 129, 0.1)',
    fill: true,
    tension: 0.3
  }]
}))

const filamentChartData = computed(() => ({
  labels: data.value?.by_filament.map(f => f.type) ?? [],
  datasets: [{
    label: 'Success Rate %',
    data: data.value?.by_filament.map(f => f.success_rate) ?? [],
    backgroundColor: '#3b82f6'
  }]
}))

async function fetchData() {
  loading.value = true
  error.value = ''
  try {
    data.value = await api.getPrints(range.value)
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to load data'
  } finally {
    loading.value = false
  }
}

watch(range, fetchData, { immediate: true })
</script>

<style scoped>
.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 24px;
}

.page-header h2 {
  font-size: 20px;
  font-weight: 600;
}

.grid-2col {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
  margin-bottom: 24px;
  align-items: start;
}

.chart-section {
  margin-bottom: 24px;
}

.chart-section h3 {
  font-size: 14px;
  font-weight: 500;
  color: var(--text-secondary);
  margin-bottom: 12px;
}

.loading, .error {
  padding: 40px;
  text-align: center;
  color: var(--text-secondary);
}

.error {
  color: var(--accent-red);
}
</style>
