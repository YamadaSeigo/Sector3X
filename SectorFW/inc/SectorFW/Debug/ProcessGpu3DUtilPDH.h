// Minimal PDH per-process 3D GPU utilization (%)
// Build: link with pdh.lib
#define NOMINMAX
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <string>
#include <vector>
#include <cwctype>

#pragma comment(lib, "pdh.lib")

namespace SFW::Debug {

    static bool wcontains(const std::wstring& s, const std::wstring& sub) {
        return s.find(sub) != std::wstring::npos;
    }

    static bool is_target_3d_instance(const std::wstring& inst, DWORD pid) {
        // Typical instance: "pid_1234_engtype_3D_0"
        // Some systems show "engtype_3d" (lowercase) in PowerShell patterns, so be tolerant.
        std::wstring pidTag = L"pid_" + std::to_wstring(pid);
        if (!wcontains(inst, pidTag)) return false;

        // Accept "engtype_3D" / "engtype_3d"
        if (wcontains(inst, L"engtype_3D") || wcontains(inst, L"engtype_3d")) return true;

        return false;
    }

	//より厳密にPIDマッチングを行う
    static bool match_pid_and_3d(const std::wstring& inst, DWORD pid)
    {
        // find "pid_"
        size_t pidPos = inst.find(L"pid_");
        if (pidPos == std::wstring::npos) return false;

        size_t numStart = pidPos + 4; // after "pid_"
        size_t numEnd = numStart;
        while (numEnd < inst.size() && iswdigit(inst[numEnd])) numEnd++;
        if (numEnd == numStart) return false;

        DWORD instPid = (DWORD)_wtoi(inst.substr(numStart, numEnd - numStart).c_str());
        if (instPid != pid) return false;

        // require engtype_3D token
        return (inst.find(L"engtype_3D") != std::wstring::npos ||
            inst.find(L"engtype_3d") != std::wstring::npos);
    }


    class ProcessGpu3DUtilPDH {
    public:
        bool init() {
            PDH_STATUS s = PdhOpenQueryW(nullptr, 0, &m_query);
            if (s != ERROR_SUCCESS) return false;

            // English counter name avoids locale issues
            s = PdhAddEnglishCounterW(m_query,
                L"\\GPU Engine(*)\\Utilization Percentage",
                0, &m_counter);
            if (s != ERROR_SUCCESS) return false;

            m_primed = false;
            return true;
        }

        void shutdown() {
            if (m_query) {
                PdhCloseQuery(m_query);
                m_query = nullptr;
                m_counter = nullptr;
            }
            m_primed = false;
        }

        // Call periodically (e.g. every 200ms..1000ms).
        // Returns true when value is valid.
        bool sample(DWORD pid, double& outPercent) {
            outPercent = 0.0;

            if (!m_query || !m_counter) return false;

            PDH_STATUS s = PdhCollectQueryData(m_query);
            if (s != ERROR_SUCCESS) return false;

            // Need at least two collects to get a non-zero cooked value
            if (!m_primed) {
                m_primed = true;
                return false;
            }

            DWORD bufSize = 0;
            DWORD itemCount = 0;

            s = PdhGetFormattedCounterArrayW(
                m_counter,
                PDH_FMT_DOUBLE,
                &bufSize,
                &itemCount,
                nullptr);

            if (s != PDH_MORE_DATA) {
                // Some systems may return ERROR_SUCCESS with itemCount=0; treat as not ready.
                return false;
            }

            std::vector<BYTE> buffer(bufSize);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());

            s = PdhGetFormattedCounterArrayW(
                m_counter,
                PDH_FMT_DOUBLE,
                &bufSize,
                &itemCount,
                items);

            if (s != ERROR_SUCCESS) return false;

            double sum = 0.0;
            for (DWORD i = 0; i < itemCount; ++i) {
                const std::wstring inst = items[i].szName ? items[i].szName : L"";
                const auto& val = items[i].FmtValue;

                if (!match_pid_and_3d(inst, pid)) continue;

                if (val.CStatus == ERROR_SUCCESS) {
                    // This is already percentage for that engine instance.
                    sum += val.doubleValue;
                }
            }

            outPercent = sum;
            return true;
        }

    private:
        PDH_HQUERY   m_query = nullptr;
        PDH_HCOUNTER m_counter = nullptr;
        bool m_primed = false;
    };

}