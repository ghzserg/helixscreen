<template>
  <div class="chart-container">
    <Bar :data="data" :options="mergedOptions" />
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { Bar } from 'vue-chartjs'
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  BarElement,
  Title,
  Tooltip,
  Legend
} from 'chart.js'
import type { ChartData, ChartOptions } from 'chart.js'

ChartJS.register(CategoryScale, LinearScale, BarElement, Title, Tooltip, Legend)

const props = defineProps<{
  data: ChartData<'bar'>
  options?: ChartOptions<'bar'>
}>()

const darkDefaults: ChartOptions<'bar'> = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: {
      labels: { color: '#94a3b8' }
    },
    tooltip: {
      backgroundColor: '#1a1d27',
      titleColor: '#e2e8f0',
      bodyColor: '#e2e8f0',
      borderColor: '#2d3348',
      borderWidth: 1
    }
  },
  scales: {
    x: {
      ticks: { color: '#94a3b8' },
      grid: { color: 'rgba(45, 51, 72, 0.5)' }
    },
    y: {
      ticks: { color: '#94a3b8' },
      grid: { color: 'rgba(45, 51, 72, 0.5)' }
    }
  }
}

const mergedOptions = computed(() => {
  return { ...darkDefaults, ...props.options } as ChartOptions<'bar'>
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
