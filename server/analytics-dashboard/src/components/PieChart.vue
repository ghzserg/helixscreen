<template>
  <div class="chart-container">
    <Pie :data="data" :options="mergedOptions" />
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { Pie } from 'vue-chartjs'
import {
  Chart as ChartJS,
  ArcElement,
  Tooltip,
  Legend
} from 'chart.js'
import type { ChartData, ChartOptions } from 'chart.js'

ChartJS.register(ArcElement, Tooltip, Legend)

const props = defineProps<{
  data: ChartData<'pie'>
  options?: ChartOptions<'pie'>
}>()

const darkDefaults: ChartOptions<'pie'> = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: {
      position: 'right',
      labels: { color: '#94a3b8', padding: 12 }
    },
    tooltip: {
      backgroundColor: '#1a1d27',
      titleColor: '#e2e8f0',
      bodyColor: '#e2e8f0',
      borderColor: '#2d3348',
      borderWidth: 1
    }
  }
}

const mergedOptions = computed(() => {
  return { ...darkDefaults, ...props.options } as ChartOptions<'pie'>
})
</script>

<style scoped>
.chart-container {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 20px;
  height: 300px;
}
</style>
