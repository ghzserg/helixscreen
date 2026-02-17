import { useAuthStore } from '@/stores/auth'
import { router } from '@/router'

const API_BASE = import.meta.env.VITE_API_BASE || 'https://telemetry.helixscreen.org'

export interface OverviewData {
  active_devices: number
  total_events: number
  crash_rate: number
  print_success_rate: number
  events_over_time: { date: string; count: number }[]
}

export interface AdoptionData {
  platforms: { name: string; count: number }[]
  versions: { name: string; count: number }[]
  printer_models: { name: string; count: number }[]
  kinematics: { name: string; count: number }[]
}

export interface PrintsData {
  success_rate_over_time: { date: string; rate: number; total: number }[]
  by_filament: { type: string; success_rate: number; count: number }[]
  avg_duration_sec: number
}

export interface CrashesData {
  by_version: { version: string; crash_count: number; session_count: number; rate: number }[]
  by_signal: { signal: string; count: number }[]
  avg_uptime_sec: number
}

export interface CrashListData {
  crashes: {
    timestamp: string
    device_id: string
    version: string
    signal: string
    platform: string
    uptime_sec: number
  }[]
}

export interface ReleasesData {
  versions: {
    version: string
    active_devices: number
    crash_rate: number
    print_success_rate: number
    total_sessions: number
    total_crashes: number
  }[]
}

async function apiFetch<T>(path: string): Promise<T> {
  const auth = useAuthStore()
  const res = await fetch(`${API_BASE}${path}`, {
    headers: {
      'X-API-Key': auth.apiKey || ''
    }
  })

  if (res.status === 401) {
    auth.logout()
    router.push('/login')
    throw new Error('Unauthorized')
  }

  if (!res.ok) {
    throw new Error(`API error: ${res.status} ${res.statusText}`)
  }

  return res.json() as Promise<T>
}

export const api = {
  getOverview(range: string): Promise<OverviewData> {
    return apiFetch(`/v1/dashboard/overview?range=${range}`)
  },

  getAdoption(range: string): Promise<AdoptionData> {
    return apiFetch(`/v1/dashboard/adoption?range=${range}`)
  },

  getPrints(range: string): Promise<PrintsData> {
    return apiFetch(`/v1/dashboard/prints?range=${range}`)
  },

  getCrashes(range: string): Promise<CrashesData> {
    return apiFetch(`/v1/dashboard/crashes?range=${range}`)
  },

  getCrashList(range: string, limit = 50): Promise<CrashListData> {
    return apiFetch(`/v1/dashboard/crash-list?range=${range}&limit=${limit}`)
  },

  getReleases(versions: string[]): Promise<ReleasesData> {
    const params = `versions=${versions.map(v => encodeURIComponent(v)).join(',')}`
    return apiFetch(`/v1/dashboard/releases?${params}`)
  }
}
