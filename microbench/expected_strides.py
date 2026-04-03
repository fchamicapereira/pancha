#!/usr/bin/env python3

from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt
import random
import os
import statistics
import math

SCRIPT_DIR = Path(__file__).parent.absolute()
OUT_DIR = SCRIPT_DIR / "plots"

# FIG_FORMAT = "pdf"
FIG_FORMAT = "png"

SAMPLE_SIZE = 100_000
FLOWS = 80_000
ELEPHANT_THRESHOLD_BY_TRAFFIC_PORTION = 0.8

EPSILON = 1e-6
ZIPF_PARAMS = [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0]
# ZIPF_PARAMS = [3, 4]

UNIV2_DATA = {
    "stride_sizes_per_batch": {
        "32": {
            "sorted": [
                4720463,
                2226765,
                1278343,
                1018366,
                736976,
                632221,
                545128,
                567016,
                541939,
                558714,
                456763,
                336880,
                301834,
                280062,
                268803,
                260146,
                239217,
                222588,
                207681,
                213429,
                240128,
                186208,
                112311,
                67572,
                33836,
                17782,
                5665,
                3502,
                2406,
                1708,
                1913,
                4536
            ],
            "unsorted": [
                44069662,
                11208109,
                3240604,
                1940035,
                643680,
                415282,
                258560,
                317645,
                142450,
                107122,
                70658,
                50663,
                40508,
                23864,
                17232,
                19160,
                7830,
                6001,
                4208,
                3497,
                2505,
                2007,
                1485,
                1243,
                871,
                759,
                636,
                549,
                443,
                400,
                354,
                4536
            ]
        },
        "64": {
            "sorted": [
                3044273,
                1417518,
                771147,
                606640,
                384955,
                350245,
                280378,
                268502,
                211027,
                201857,
                178541,
                156969,
                144144,
                142519,
                139040,
                139178,
                139307,
                141276,
                138735,
                138492,
                145038,
                113847,
                100413,
                93352,
                88222,
                85912,
                83671,
                83813,
                78232,
                76342,
                73967,
                70564,
                67401,
                65636,
                61043,
                59538,
                53869,
                51131,
                48251,
                47390,
                46299,
                49878,
                41907,
                26651,
                22159,
                16366,
                10343,
                6342,
                3941,
                2831,
                1855,
                749,
                489,
                422,
                376,
                374,
                317,
                347,
                313,
                290,
                359,
                338,
                514,
                920
            ],
            "unsorted": [
                43491094,
                11151013,
                3207676,
                1963894,
                632580,
                408759,
                251736,
                338060,
                147766,
                114111,
                75513,
                55211,
                46047,
                26564,
                19279,
                24512,
                9166,
                7307,
                5081,
                4347,
                3067,
                2560,
                1966,
                1730,
                999,
                898,
                667,
                629,
                424,
                684,
                466,
                410,
                192,
                197,
                177,
                209,
                173,
                165,
                139,
                170,
                120,
                123,
                135,
                117,
                144,
                119,
                103,
                94,
                86,
                76,
                87,
                97,
                76,
                77,
                57,
                57,
                70,
                65,
                69,
                59,
                37,
                38,
                42,
                920
            ]
        },
        "128": {
            "sorted": [
                2133528,
                990448,
                513754,
                438912,
                251420,
                219780,
                161597,
                170841,
                113354,
                110827,
                96962,
                93612,
                79929,
                80318,
                70155,
                67765,
                60457,
                58241,
                54145,
                52851,
                50488,
                45970,
                44693,
                42800,
                40248,
                39967,
                39062,
                38137,
                36342,
                36134,
                35896,
                35853,
                35402,
                35270,
                34520,
                35145,
                34378,
                34788,
                34675,
                35630,
                36104,
                38546,
                35560,
                30079,
                28803,
                28598,
                28395,
                27925,
                27561,
                27193,
                26996,
                26904,
                26351,
                25902,
                24736,
                23237,
                23604,
                21345,
                20142,
                19271,
                18080,
                18111,
                17165,
                16702,
                16798,
                17231,
                16449,
                16681,
                17552,
                16992,
                16228,
                15183,
                14980,
                13674,
                12814,
                11416,
                10624,
                10084,
                8774,
                8560,
                7682,
                7475,
                6987,
                6998,
                6821,
                4030,
                2031,
                1410,
                1068,
                916,
                610,
                468,
                369,
                289,
                221,
                225,
                190,
                181,
                168,
                139,
                140,
                108,
                80,
                87,
                92,
                85,
                80,
                78,
                78,
                61,
                50,
                65,
                63,
                56,
                56,
                55,
                52,
                52,
                52,
                48,
                63,
                68,
                46,
                67,
                78,
                86,
                124,
                128
            ],
            "unsorted": [
                43202863,
                11121893,
                3191174,
                1976317,
                626578,
                405578,
                248491,
                348225,
                150253,
                117595,
                77901,
                57414,
                48872,
                27922,
                20339,
                27369,
                9815,
                7970,
                5422,
                4775,
                3402,
                2843,
                2157,
                1976,
                1073,
                1015,
                656,
                711,
                421,
                813,
                498,
                456,
                190,
                198,
                150,
                202,
                156,
                132,
                120,
                156,
                107,
                110,
                117,
                109,
                103,
                104,
                100,
                95,
                86,
                71,
                71,
                80,
                76,
                69,
                56,
                57,
                74,
                63,
                58,
                45,
                37,
                37,
                44,
                50,
                35,
                44,
                30,
                45,
                44,
                31,
                35,
                27,
                31,
                24,
                21,
                25,
                28,
                36,
                28,
                34,
                34,
                36,
                15,
                13,
                17,
                25,
                21,
                14,
                10,
                21,
                17,
                13,
                30,
                12,
                13,
                15,
                11,
                10,
                8,
                11,
                11,
                13,
                7,
                6,
                10,
                5,
                3,
                6,
                14,
                6,
                6,
                5,
                5,
                6,
                10,
                6,
                6,
                4,
                5,
                6,
                2,
                4,
                7,
                6,
                5,
                5,
                1,
                128
            ]
        },
    }
}

EQUINIX_NYC_DATA = {
    "stride_sizes_per_batch": {
    "32": {
      "sorted": [
        164005229,
        39329289,
        11703457,
        9081909,
        3098401,
        1962508,
        899820,
        712126,
        415004,
        325425,
        213341,
        235307,
        123959,
        97733,
        75047,
        61698,
        48530,
        40693,
        32539,
        26870,
        22399,
        19175,
        16860,
        14585,
        11849,
        9263,
        6517,
        3831,
        1873,
        710,
        211,
        67
      ],
      "unsorted": [
        278284326,
        32467260,
        5313850,
        1948357,
        603220,
        318044,
        164067,
        102439,
        61692,
        40392,
        26003,
        22375,
        10310,
        6801,
        4716,
        3271,
        2266,
        1574,
        1131,
        785,
        558,
        417,
        309,
        228,
        137,
        100,
        73,
        66,
        49,
        32,
        21,
        67
      ]
    },
    "64": {
      "sorted": [
        148098387,
        35261917,
        9250363,
        7611176,
        3113322,
        2788702,
        1622341,
        1475860,
        770335,
        592902,
        337981,
        341684,
        191405,
        158293,
        119390,
        105750,
        74993,
        71690,
        54587,
        49982,
        40233,
        36072,
        31419,
        39266,
        24835,
        20721,
        17899,
        16476,
        14155,
        13885,
        12556,
        10854,
        9832,
        8708,
        7885,
        8153,
        6306,
        5464,
        4845,
        4208,
        3999,
        3559,
        3196,
        2765,
        4000,
        1883,
        1244,
        1106,
        890,
        748,
        612,
        536,
        399,
        321,
        199,
        120,
        74,
        44,
        14,
        9,
        8,
        2,
        0,
        0
      ],
      "unsorted": [
        276975810,
        32700943,
        5373817,
        1997979,
        616128,
        328572,
        170159,
        107856,
        65619,
        43627,
        28307,
        26145,
        11753,
        7908,
        5506,
        3948,
        2772,
        1979,
        1432,
        1076,
        753,
        574,
        421,
        312,
        237,
        166,
        130,
        102,
        79,
        59,
        36,
        36,
        39,
        12,
        20,
        18,
        21,
        8,
        9,
        7,
        6,
        4,
        2,
        3,
        3,
        5,
        1,
        1,
        3,
        0,
        0,
        2,
        1,
        3,
        0,
        1,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0
      ]
    },
    "128": {
      "sorted": [
        135932148,
        33114519,
        8267443,
        6295565,
        2588997,
        2417059,
        1427232,
        1540413,
        887985,
        834703,
        518655,
        632336,
        368706,
        309791,
        223083,
        204044,
        130374,
        126852,
        84930,
        80969,
        60083,
        53841,
        43635,
        48983,
        36011,
        30865,
        25806,
        25052,
        21235,
        21027,
        18731,
        17423,
        15750,
        14509,
        13454,
        16030,
        12099,
        10319,
        9369,
        8908,
        8157,
        7732,
        8276,
        8315,
        16863,
        8754,
        5376,
        7178,
        4939,
        4047,
        3759,
        3439,
        3190,
        3075,
        2841,
        2752,
        2655,
        2420,
        2300,
        2830,
        2119,
        1869,
        1770,
        1569,
        1543,
        1401,
        1322,
        1241,
        1188,
        1080,
        994,
        1077,
        884,
        793,
        682,
        636,
        558,
        540,
        467,
        461,
        399,
        412,
        335,
        317,
        301,
        277,
        237,
        195,
        213,
        208,
        202,
        164,
        131,
        161,
        122,
        121,
        104,
        94,
        88,
        97,
        99,
        56,
        60,
        44,
        48,
        24,
        26,
        20,
        14,
        3,
        5,
        5,
        4,
        6,
        0,
        1,
        0,
        1,
        1,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0
      ],
      "unsorted": [
        276320349,
        32818339,
        5403939,
        2022490,
        622174,
        333884,
        173336,
        110480,
        67485,
        45441,
        29432,
        28018,
        12551,
        8413,
        5901,
        4293,
        2979,
        2188,
        1592,
        1204,
        880,
        665,
        493,
        358,
        271,
        198,
        172,
        131,
        99,
        75,
        46,
        48,
        47,
        28,
        24,
        22,
        23,
        12,
        11,
        10,
        7,
        7,
        6,
        8,
        5,
        6,
        3,
        1,
        3,
        0,
        1,
        2,
        0,
        1,
        1,
        1,
        1,
        0,
        0,
        1,
        0,
        0,
        1,
        0,
        2,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0
      ]
    },
  }
}

# Change plot font size
plt.rcParams.update({'font.size': 17})

def uniform_distribution(start: int = 0, end: int = FLOWS - 1, sample_size: int = SAMPLE_SIZE) -> list[int]:
    flows = []
    for i in range(sample_size):
        flow = random.randint(start, end)
        flows.append(flow)
        print(f"Generating uniform flows ({int(100 * (i + 1) / sample_size):3}%)", end="\r")
    print()
    return flows


def zipf_distribution(zipf_param: float, start: int = 0, end: int = FLOWS - 1, sample_size: int = SAMPLE_SIZE) -> list[int]:
    if zipf_param == 0 or zipf_param == 1:
        zipf_param += EPSILON

    flows = []
    # N = FLOWS + 1
    N = end - start + 2
    for i in range(sample_size):
        p = random.random()
        x = N / 2.0

        D = p * (12.0 * (N ** (1.0 - zipf_param) - 1) / (1.0 - zipf_param) + 6.0 - 6.0 * N ** (-zipf_param) + zipf_param - N ** (-1.0 - zipf_param) * zipf_param)

        while True:
            m = x ** (-2 - zipf_param)
            mx = m * x
            mxx = mx * x
            mxxx = mxx * x

            a = 12.0 * (mxxx - 1) / (1.0 - zipf_param) + 6.0 * (1.0 - mxx) + (zipf_param - (mx * zipf_param)) - D
            b = 12.0 * mxx + 6.0 * (zipf_param * mx) + (m * zipf_param * (zipf_param + 1.0))
            newx = max(1.0, x - a / b)

            if abs(newx - x) <= 0.01:
                flow_id = int(newx - 1)
                assert flow_id < FLOWS and "Invalid index"
                flow_id += start
                flows.append(flow_id)
                break

            x = newx

        print(f"Generating zipfian flows s={zipf_param:.1f} ({int(100 * (i + 1) / sample_size):3}%)", end="\r")
    print()
    return flows

def get_stride_sizes_from_flows(flows: list[int], sort: bool = False) -> list[int]:
    batches = [flows[i:i+32] for i in range(0, len(flows), 32)]
    stride_sizes = [0] * 32

    for batch in batches:
        if sort:
            batch.sort()
        strides = [1]
        for i in range(len(batch) - 1):
            if batch[i] == batch[i+1]:
                strides[-1] += 1
            else:
                strides.append(1)
        for stride in strides:
            assert stride <= 32 and "Invalid stride size"
            stride_sizes[stride - 1] += 1
    
    return stride_sizes

def plot_hist_strides(strides_per_experiment: dict[str, list[int]], exp_name: str):
    out_file = OUT_DIR / f"{exp_name}.{FIG_FORMAT}"
    print(f"Plotting {len(strides_per_experiment)} histograms to {out_file}...")

    # Let's try to make a grid of subplots, as wide as high as possible
    nx = math.ceil(math.sqrt(len(strides_per_experiment)))

    fig, axs = plt.subplots(nx, nx, figsize=(12 * nx, 6 * nx), squeeze=False)


    for ax, (name, stride_sizes) in zip(axs.flatten(), strides_per_experiment.items()):
        total_strides_count = sum(stride_sizes)
        rel_stride_sizes = [ 100.0 * c / total_strides_count for c in stride_sizes ]
        
        stride_sizes = [ i+1 for i in range(len(stride_sizes)) ]
        ax.bar(stride_sizes, rel_stride_sizes)
        ax.set_ylim(0, 100)
        ax.set_yticks([ y for y in range(0, 101, 10) ])

        # Have at most 10 xticks, evenly spaced
        if len(stride_sizes) <= 10:
            ax.set_xticks(stride_sizes)
        else:
            ax.set_xticks([ stride_sizes[i] for i in range(0, len(stride_sizes), len(stride_sizes) // 10) ])

        ax.set_ylabel('Traffic (%)')
        ax.set_xlabel('Stride Size')
        ax.set_title(name)
    
    if len(strides_per_experiment) < nx * nx:
        for i in range(len(strides_per_experiment), nx * nx):
            fig.delaxes(axs.flatten()[i])

    plt.tight_layout()
    plt.savefig(out_file, dpi=500)
    plt.close()

def filter_by_elephants(flows: list[int], threshold_traffic_portion: float) -> tuple[set[int], list[int]]:
    flow_counts = {}
    for flow in flows:
        if flow not in flow_counts:
            flow_counts[flow] = 0
        flow_counts[flow] += 1
    flows_set = set(flows)
    sorted_flows = sorted(list(flows_set), key=lambda f: flow_counts[f], reverse=True)
    elephants = set()
    
    for flow in sorted_flows:
        if sum([ flow_counts[e] for e in elephants ]) < threshold_traffic_portion * len(flows):
            elephants.add(flow)
        else:
            break
        print(f"Filtering elephants ({int(100 * len(elephants) / len(flows_set)):3}%)", end="\r")
    
    print(f"Found {len(elephants)} elephants that account for {100.0 * sum([ flow_counts[e] for e in elephants ]) / len(flows):.2f}% of the traffic")

    return elephants, [ f for f in flows if f in elephants ]

if __name__ == "__main__":
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    experiments = {f"s={s:.1f}": zipf_distribution(s, 0, FLOWS - 1, SAMPLE_SIZE) for s in ZIPF_PARAMS}
    stride_sizes_unsorted = { f"{exp_name} (unsorted)": get_stride_sizes_from_flows(flows, sort=False) for exp_name, flows in experiments.items() }
    stride_sizes_sorted = { f"{exp_name} (sorted)": get_stride_sizes_from_flows(flows, sort=True) for exp_name, flows in experiments.items() }

    plot_hist_strides(stride_sizes_unsorted, "stride_sizes_unsorted")
    plot_hist_strides(stride_sizes_sorted, "stride_sizes_sorted")

    univ2_stride_sizes_unsorted = { f"univ2 batch size {batch_size} (unsorted)": UNIV2_DATA["stride_sizes_per_batch"][batch_size]["unsorted"] for batch_size in UNIV2_DATA["stride_sizes_per_batch"] }
    univ2_stride_sizes_sorted = { f"univ2 batch size {batch_size} (sorted)": UNIV2_DATA["stride_sizes_per_batch"][batch_size]["sorted"] for batch_size in UNIV2_DATA["stride_sizes_per_batch"] }

    plot_hist_strides(univ2_stride_sizes_unsorted, "imc10_univ2_stride_sizes_unsorted")
    plot_hist_strides(univ2_stride_sizes_sorted, "imc10_univ2_stride_sizes_sorted")

    equinix_nyc_stride_sizes_unsorted = { f"equinix nyc batch size {batch_size} (unsorted)": EQUINIX_NYC_DATA["stride_sizes_per_batch"][batch_size]["unsorted"] for batch_size in EQUINIX_NYC_DATA["stride_sizes_per_batch"] }
    equinix_nyc_stride_sizes_sorted = { f"equinix nyc batch size {batch_size} (sorted)": EQUINIX_NYC_DATA["stride_sizes_per_batch"][batch_size]["sorted"] for batch_size in EQUINIX_NYC_DATA["stride_sizes_per_batch"] }

    plot_hist_strides(equinix_nyc_stride_sizes_unsorted, "equinix_nyc_stride_sizes_unsorted")
    plot_hist_strides(equinix_nyc_stride_sizes_sorted, "equinix_nyc_stride_sizes_sorted")

    # experiments = {}
    # for s in ZIPF_PARAMS:
    #     flows = zipf_distribution(s, 0, FLOWS - 1, SAMPLE_SIZE)
    #     elephants, elephant_flows = filter_by_elephants(flows, ELEPHANT_THRESHOLD_BY_TRAFFIC_PORTION)
    #     experiments[f"s={s:.1f} #elephants={len(elephants)} threshold={ELEPHANT_THRESHOLD_BY_TRAFFIC_PORTION:3.0f}%"] = elephant_flows
    
    # stride_sizes_unsorted = { f"{exp_name} (unsorted)": get_stride_sizes_from_flows(flows, sort=False) for exp_name, flows in experiments.items() }
    # stride_sizes_sorted = { f"{exp_name} (sorted)": get_stride_sizes_from_flows(flows, sort=True) for exp_name, flows in experiments.items() }

    # plot_hist_strides(stride_sizes_unsorted, "stride_sizes_unsorted_elephants_only")
    # plot_hist_strides(stride_sizes_sorted, "stride_sizes_sorted_elephants_only")


