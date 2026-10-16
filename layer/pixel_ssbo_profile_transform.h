#pragma once

// Cold canonical transformer. Tables below contain only instructions generated
// by our original-invocation SSBO exporter and new owned interface types; no
// original shader body is distributed. Exact original bytes come exclusively
// from a successful native shader callback and must match the private profile
// pin before being copied. No shader compiler, Vulkan call, I/O or activation.
#include "pixel_ssbo_clone_contract.h"

namespace frame_pixel_ssbo_profile {
namespace detail {
inline constexpr std::uint32_t annotations[]={
    262215u,1010u,6u,4u,196679u,1011u,3u,327752u,1011u,0u,
    35u,0u,262215u,1014u,34u,1u,262215u,1014u,33u,0u,
};
inline constexpr std::uint32_t globals[]={
    196637u,1010u,6u,196638u,1011u,1010u,262176u,1012u,2u,1011u,
    262176u,1013u,2u,6u,262203u,1012u,1014u,2u,262187u,8u,
    1017u,0u,262187u,8u,1019u,1065353216u,262187u,6u,1031u,0u,
    262187u,6u,1032u,1u,262187u,6u,1036u,1397965391u,262187u,6u,
    1037u,32u,262187u,6u,1093u,2u,262187u,6u,1095u,3u,
    262187u,6u,1097u,4u,262187u,6u,1099u,5u,262187u,6u,
    1101u,6u,262187u,6u,1103u,7u,262187u,6u,1105u,8u,
    262187u,6u,1107u,9u,262187u,6u,1109u,10u,262187u,6u,
    1111u,11u,262187u,6u,1113u,12u,262187u,6u,1115u,13u,
    262187u,6u,1117u,14u,262187u,6u,1119u,15u,262187u,6u,
    1121u,16u,262187u,6u,1123u,17u,262187u,6u,1125u,18u,
    262187u,6u,1127u,19u,262187u,6u,1129u,20u,262187u,6u,
    1131u,21u,262187u,6u,1133u,22u,262187u,6u,1135u,23u,
    262187u,6u,1137u,24u,262187u,6u,1139u,25u,262187u,6u,
    1141u,26u,262187u,6u,1143u,27u,262187u,6u,1145u,28u,
    262187u,6u,1147u,29u,262187u,6u,1149u,30u,262187u,6u,
    1151u,31u,
};
inline constexpr std::uint32_t pixel_body[]={
    262205u,9u,1015u,487u,327761u,8u,1016u,1015u,0u,327870u,
    96u,1018u,1016u,1017u,327864u,96u,1020u,1016u,1019u,327847u,
    96u,1021u,1018u,1020u,327761u,8u,1022u,1015u,1u,327870u,
    96u,1023u,1022u,1017u,327864u,96u,1024u,1022u,1019u,327847u,
    96u,1025u,1023u,1024u,327847u,96u,1026u,1021u,1025u,196855u,
    1030u,0u,262394u,1026u,1027u,1030u,131320u,1027u,393281u,1013u,
    1033u,1014u,1031u,1031u,590054u,6u,1034u,1033u,1032u,1031u,
    1031u,1032u,1031u,327850u,96u,1035u,1034u,1031u,196855u,1029u,
    0u,262394u,1035u,1028u,1029u,131320u,1028u,327761u,8u,1038u,
    538u,0u,262268u,6u,1039u,1038u,327761u,8u,1040u,538u,
    1u,262268u,6u,1041u,1040u,327761u,8u,1042u,871u,0u,
    262268u,6u,1043u,1042u,327761u,8u,1044u,871u,1u,262268u,
    6u,1045u,1044u,327761u,8u,1046u,871u,2u,262268u,6u,
    1047u,1046u,327761u,8u,1048u,871u,3u,262268u,6u,1049u,
    1048u,327761u,8u,1050u,777u,0u,262268u,6u,1051u,1050u,
    327761u,8u,1052u,777u,1u,262268u,6u,1053u,1052u,327761u,
    8u,1054u,777u,2u,262268u,6u,1055u,1054u,327761u,8u,
    1056u,777u,3u,262268u,6u,1057u,1056u,327761u,8u,1058u,
    797u,0u,262268u,6u,1059u,1058u,327761u,8u,1060u,797u,
    1u,262268u,6u,1061u,1060u,327761u,8u,1062u,907u,0u,
    262268u,6u,1063u,1062u,327761u,8u,1064u,907u,1u,262268u,
    6u,1065u,1064u,327761u,8u,1066u,907u,2u,262268u,6u,
    1067u,1066u,327761u,8u,1068u,603u,0u,262268u,6u,1069u,
    1068u,327761u,8u,1070u,603u,1u,262268u,6u,1071u,1070u,
    327761u,8u,1072u,603u,2u,262268u,6u,1073u,1072u,327761u,
    8u,1074u,603u,3u,262268u,6u,1075u,1074u,327761u,8u,
    1076u,629u,0u,262268u,6u,1077u,1076u,327761u,8u,1078u,
    629u,1u,262268u,6u,1079u,1078u,327761u,8u,1080u,629u,
    2u,262268u,6u,1081u,1080u,327761u,8u,1082u,629u,3u,
    262268u,6u,1083u,1082u,327761u,8u,1084u,994u,0u,262268u,
    6u,1085u,1084u,327761u,8u,1086u,994u,1u,262268u,6u,
    1087u,1086u,327761u,8u,1088u,994u,2u,262268u,6u,1089u,
    1088u,327761u,8u,1090u,994u,3u,262268u,6u,1091u,1090u,
    393281u,1013u,1092u,1014u,1031u,1032u,196670u,1092u,1036u,393281u,
    1013u,1094u,1014u,1031u,1093u,196670u,1094u,1032u,393281u,1013u,
    1096u,1014u,1031u,1095u,196670u,1096u,1037u,393281u,1013u,1098u,
    1014u,1031u,1097u,196670u,1098u,1039u,393281u,1013u,1100u,1014u,
    1031u,1099u,196670u,1100u,1041u,393281u,1013u,1102u,1014u,1031u,
    1101u,196670u,1102u,1043u,393281u,1013u,1104u,1014u,1031u,1103u,
    196670u,1104u,1045u,393281u,1013u,1106u,1014u,1031u,1105u,196670u,
    1106u,1047u,393281u,1013u,1108u,1014u,1031u,1107u,196670u,1108u,
    1049u,393281u,1013u,1110u,1014u,1031u,1109u,196670u,1110u,1051u,
    393281u,1013u,1112u,1014u,1031u,1111u,196670u,1112u,1053u,393281u,
    1013u,1114u,1014u,1031u,1113u,196670u,1114u,1055u,393281u,1013u,
    1116u,1014u,1031u,1115u,196670u,1116u,1057u,393281u,1013u,1118u,
    1014u,1031u,1117u,196670u,1118u,1059u,393281u,1013u,1120u,1014u,
    1031u,1119u,196670u,1120u,1061u,393281u,1013u,1122u,1014u,1031u,
    1121u,196670u,1122u,709u,393281u,1013u,1124u,1014u,1031u,1123u,
    196670u,1124u,1063u,393281u,1013u,1126u,1014u,1031u,1125u,196670u,
    1126u,1065u,393281u,1013u,1128u,1014u,1031u,1127u,196670u,1128u,
    1067u,393281u,1013u,1130u,1014u,1031u,1129u,196670u,1130u,1069u,
    393281u,1013u,1132u,1014u,1031u,1131u,196670u,1132u,1071u,393281u,
    1013u,1134u,1014u,1031u,1133u,196670u,1134u,1073u,393281u,1013u,
    1136u,1014u,1031u,1135u,196670u,1136u,1075u,393281u,1013u,1138u,
    1014u,1031u,1137u,196670u,1138u,1077u,393281u,1013u,1140u,1014u,
    1031u,1139u,196670u,1140u,1079u,393281u,1013u,1142u,1014u,1031u,
    1141u,196670u,1142u,1081u,393281u,1013u,1144u,1014u,1031u,1143u,
    196670u,1144u,1083u,393281u,1013u,1146u,1014u,1031u,1145u,196670u,
    1146u,1085u,393281u,1013u,1148u,1014u,1031u,1147u,196670u,1148u,
    1087u,393281u,1013u,1150u,1014u,1031u,1149u,196670u,1150u,1089u,
    393281u,1013u,1152u,1014u,1031u,1151u,196670u,1152u,1091u,131321u,
    1029u,131320u,1029u,131321u,1030u,131320u,1030u,
};
inline constexpr std::uint32_t first_body[]={
    393281u,1013u,1033u,1014u,1031u,1031u,590054u,6u,1034u,1033u,
    1032u,1031u,1031u,1032u,1031u,327850u,96u,1035u,1034u,1031u,
    196855u,1029u,0u,262394u,1035u,1028u,1029u,131320u,1028u,327761u,
    8u,1038u,538u,0u,262268u,6u,1039u,1038u,327761u,8u,
    1040u,538u,1u,262268u,6u,1041u,1040u,327761u,8u,1042u,
    871u,0u,262268u,6u,1043u,1042u,327761u,8u,1044u,871u,
    1u,262268u,6u,1045u,1044u,327761u,8u,1046u,871u,2u,
    262268u,6u,1047u,1046u,327761u,8u,1048u,871u,3u,262268u,
    6u,1049u,1048u,327761u,8u,1050u,777u,0u,262268u,6u,
    1051u,1050u,327761u,8u,1052u,777u,1u,262268u,6u,1053u,
    1052u,327761u,8u,1054u,777u,2u,262268u,6u,1055u,1054u,
    327761u,8u,1056u,777u,3u,262268u,6u,1057u,1056u,327761u,
    8u,1058u,797u,0u,262268u,6u,1059u,1058u,327761u,8u,
    1060u,797u,1u,262268u,6u,1061u,1060u,327761u,8u,1062u,
    907u,0u,262268u,6u,1063u,1062u,327761u,8u,1064u,907u,
    1u,262268u,6u,1065u,1064u,327761u,8u,1066u,907u,2u,
    262268u,6u,1067u,1066u,327761u,8u,1068u,603u,0u,262268u,
    6u,1069u,1068u,327761u,8u,1070u,603u,1u,262268u,6u,
    1071u,1070u,327761u,8u,1072u,603u,2u,262268u,6u,1073u,
    1072u,327761u,8u,1074u,603u,3u,262268u,6u,1075u,1074u,
    327761u,8u,1076u,629u,0u,262268u,6u,1077u,1076u,327761u,
    8u,1078u,629u,1u,262268u,6u,1079u,1078u,327761u,8u,
    1080u,629u,2u,262268u,6u,1081u,1080u,327761u,8u,1082u,
    629u,3u,262268u,6u,1083u,1082u,327761u,8u,1084u,994u,
    0u,262268u,6u,1085u,1084u,327761u,8u,1086u,994u,1u,
    262268u,6u,1087u,1086u,327761u,8u,1088u,994u,2u,262268u,
    6u,1089u,1088u,327761u,8u,1090u,994u,3u,262268u,6u,
    1091u,1090u,393281u,1013u,1092u,1014u,1031u,1032u,196670u,1092u,
    1036u,393281u,1013u,1094u,1014u,1031u,1093u,196670u,1094u,1032u,
    393281u,1013u,1096u,1014u,1031u,1095u,196670u,1096u,1037u,393281u,
    1013u,1098u,1014u,1031u,1097u,196670u,1098u,1039u,393281u,1013u,
    1100u,1014u,1031u,1099u,196670u,1100u,1041u,393281u,1013u,1102u,
    1014u,1031u,1101u,196670u,1102u,1043u,393281u,1013u,1104u,1014u,
    1031u,1103u,196670u,1104u,1045u,393281u,1013u,1106u,1014u,1031u,
    1105u,196670u,1106u,1047u,393281u,1013u,1108u,1014u,1031u,1107u,
    196670u,1108u,1049u,393281u,1013u,1110u,1014u,1031u,1109u,196670u,
    1110u,1051u,393281u,1013u,1112u,1014u,1031u,1111u,196670u,1112u,
    1053u,393281u,1013u,1114u,1014u,1031u,1113u,196670u,1114u,1055u,
    393281u,1013u,1116u,1014u,1031u,1115u,196670u,1116u,1057u,393281u,
    1013u,1118u,1014u,1031u,1117u,196670u,1118u,1059u,393281u,1013u,
    1120u,1014u,1031u,1119u,196670u,1120u,1061u,393281u,1013u,1122u,
    1014u,1031u,1121u,196670u,1122u,709u,393281u,1013u,1124u,1014u,
    1031u,1123u,196670u,1124u,1063u,393281u,1013u,1126u,1014u,1031u,
    1125u,196670u,1126u,1065u,393281u,1013u,1128u,1014u,1031u,1127u,
    196670u,1128u,1067u,393281u,1013u,1130u,1014u,1031u,1129u,196670u,
    1130u,1069u,393281u,1013u,1132u,1014u,1031u,1131u,196670u,1132u,
    1071u,393281u,1013u,1134u,1014u,1031u,1133u,196670u,1134u,1073u,
    393281u,1013u,1136u,1014u,1031u,1135u,196670u,1136u,1075u,393281u,
    1013u,1138u,1014u,1031u,1137u,196670u,1138u,1077u,393281u,1013u,
    1140u,1014u,1031u,1139u,196670u,1140u,1079u,393281u,1013u,1142u,
    1014u,1031u,1141u,196670u,1142u,1081u,393281u,1013u,1144u,1014u,
    1031u,1143u,196670u,1144u,1083u,393281u,1013u,1146u,1014u,1031u,
    1145u,196670u,1146u,1085u,393281u,1013u,1148u,1014u,1031u,1147u,
    196670u,1148u,1087u,393281u,1013u,1150u,1014u,1031u,1149u,196670u,
    1150u,1089u,393281u,1013u,1152u,1014u,1031u,1151u,196670u,1152u,
    1091u,131321u,1029u,131320u,1029u,
};
inline constexpr std::uint32_t interface_types[]={
    262187u,6u,1153u,1u,262172u,1154u,286u,1153u,262176u,1155u,
    0u,1154u,
};
inline constexpr std::uint32_t interface_variable[]={
    262203u,1155u,289u,0u,
};
inline constexpr std::uint32_t insertion_offsets[]={404u,738u,1650u};
inline constexpr std::uint32_t variable_offset=637u;
inline constexpr std::uint32_t output_bound=1156u;
inline constexpr std::uint32_t original_bound=1010u;
inline constexpr std::uint32_t sparse_globals[]={
    196637u,1010u,6u,196638u,1011u,1010u,262176u,1012u,2u,1011u,
    262176u,1013u,2u,6u,262203u,1012u,1014u,2u,262187u,8u,
    1017u,0u,262187u,8u,1019u,1065353216u,262187u,6u,1031u,0u,
    262187u,6u,1032u,1u,262187u,6u,1036u,1397965391u,262187u,6u,
    1037u,32u,262187u,6u,1093u,2u,262187u,6u,1095u,3u,
    262187u,6u,1097u,4u,262187u,6u,1099u,5u,262187u,6u,
    1101u,6u,262187u,6u,1103u,7u,262187u,6u,1105u,8u,
    262187u,6u,1107u,9u,262187u,6u,1109u,10u,262187u,6u,
    1111u,11u,262187u,6u,1113u,12u,262187u,6u,1115u,13u,
    262187u,6u,1117u,14u,262187u,6u,1119u,15u,262187u,6u,
    1121u,16u,262187u,6u,1123u,17u,262187u,6u,1125u,18u,
    262187u,6u,1127u,19u,262187u,6u,1129u,20u,262187u,6u,
    1131u,21u,262187u,6u,1133u,22u,262187u,6u,1135u,23u,
    262187u,6u,1137u,24u,262187u,6u,1139u,25u,262187u,6u,
    1141u,26u,262187u,6u,1143u,27u,262187u,6u,1145u,28u,
    262187u,6u,1147u,29u,262187u,6u,1149u,30u,262187u,6u,
    1151u,31u,262187u,6u,1153u,0u,262187u,6u,1154u,31u,
};
inline constexpr std::uint32_t sparse_body[]={
    262205u,9u,1015u,487u,327761u,8u,1016u,1015u,0u,327761u,
    8u,1022u,1015u,1u,262253u,6u,1155u,1016u,262253u,6u,
    1156u,1022u,327879u,6u,1157u,1155u,1154u,327879u,6u,1158u,
    1156u,1154u,327850u,96u,1159u,1157u,1153u,327850u,96u,1160u,
    1158u,1153u,327847u,96u,1161u,1159u,1160u,196855u,1030u,0u,
    262394u,1161u,1027u,1030u,131320u,1027u,393281u,1013u,1033u,1014u,
    1031u,1031u,590054u,6u,1034u,1033u,1032u,1031u,1031u,1032u,
    1031u,327850u,96u,1035u,1034u,1031u,196855u,1029u,0u,262394u,
    1035u,1028u,1029u,131320u,1028u,327761u,8u,1038u,538u,0u,
    262268u,6u,1039u,1038u,327761u,8u,1040u,538u,1u,262268u,
    6u,1041u,1040u,327761u,8u,1042u,871u,0u,262268u,6u,
    1043u,1042u,327761u,8u,1044u,871u,1u,262268u,6u,1045u,
    1044u,327761u,8u,1046u,871u,2u,262268u,6u,1047u,1046u,
    327761u,8u,1048u,871u,3u,262268u,6u,1049u,1048u,327761u,
    8u,1050u,777u,0u,262268u,6u,1051u,1050u,327761u,8u,
    1052u,777u,1u,262268u,6u,1053u,1052u,327761u,8u,1054u,
    777u,2u,262268u,6u,1055u,1054u,327761u,8u,1056u,777u,
    3u,262268u,6u,1057u,1056u,327761u,8u,1058u,797u,0u,
    262268u,6u,1059u,1058u,327761u,8u,1060u,797u,1u,262268u,
    6u,1061u,1060u,327761u,8u,1062u,907u,0u,262268u,6u,
    1063u,1062u,327761u,8u,1064u,907u,1u,262268u,6u,1065u,
    1064u,327761u,8u,1066u,907u,2u,262268u,6u,1067u,1066u,
    327761u,8u,1068u,603u,0u,262268u,6u,1069u,1068u,327761u,
    8u,1070u,603u,1u,262268u,6u,1071u,1070u,327761u,8u,
    1072u,603u,2u,262268u,6u,1073u,1072u,327761u,8u,1074u,
    603u,3u,262268u,6u,1075u,1074u,327761u,8u,1076u,629u,
    0u,262268u,6u,1077u,1076u,327761u,8u,1078u,629u,1u,
    262268u,6u,1079u,1078u,327761u,8u,1080u,629u,2u,262268u,
    6u,1081u,1080u,327761u,8u,1082u,629u,3u,262268u,6u,
    1083u,1082u,327761u,8u,1084u,994u,0u,262268u,6u,1085u,
    1084u,327761u,8u,1086u,994u,1u,262268u,6u,1087u,1086u,
    327761u,8u,1088u,994u,2u,262268u,6u,1089u,1088u,327761u,
    8u,1090u,994u,3u,262268u,6u,1091u,1090u,393281u,1013u,
    1092u,1014u,1031u,1032u,196670u,1092u,1036u,393281u,1013u,1094u,
    1014u,1031u,1093u,196670u,1094u,1032u,393281u,1013u,1096u,1014u,
    1031u,1095u,196670u,1096u,1037u,393281u,1013u,1098u,1014u,1031u,
    1097u,196670u,1098u,1039u,393281u,1013u,1100u,1014u,1031u,1099u,
    196670u,1100u,1041u,393281u,1013u,1102u,1014u,1031u,1101u,196670u,
    1102u,1043u,393281u,1013u,1104u,1014u,1031u,1103u,196670u,1104u,
    1045u,393281u,1013u,1106u,1014u,1031u,1105u,196670u,1106u,1047u,
    393281u,1013u,1108u,1014u,1031u,1107u,196670u,1108u,1049u,393281u,
    1013u,1110u,1014u,1031u,1109u,196670u,1110u,1051u,393281u,1013u,
    1112u,1014u,1031u,1111u,196670u,1112u,1053u,393281u,1013u,1114u,
    1014u,1031u,1113u,196670u,1114u,1055u,393281u,1013u,1116u,1014u,
    1031u,1115u,196670u,1116u,1057u,393281u,1013u,1118u,1014u,1031u,
    1117u,196670u,1118u,1059u,393281u,1013u,1120u,1014u,1031u,1119u,
    196670u,1120u,1061u,393281u,1013u,1122u,1014u,1031u,1121u,196670u,
    1122u,709u,393281u,1013u,1124u,1014u,1031u,1123u,196670u,1124u,
    1063u,393281u,1013u,1126u,1014u,1031u,1125u,196670u,1126u,1065u,
    393281u,1013u,1128u,1014u,1031u,1127u,196670u,1128u,1067u,393281u,
    1013u,1130u,1014u,1031u,1129u,196670u,1130u,1069u,393281u,1013u,
    1132u,1014u,1031u,1131u,196670u,1132u,1071u,393281u,1013u,1134u,
    1014u,1031u,1133u,196670u,1134u,1073u,393281u,1013u,1136u,1014u,
    1031u,1135u,196670u,1136u,1075u,393281u,1013u,1138u,1014u,1031u,
    1137u,196670u,1138u,1077u,393281u,1013u,1140u,1014u,1031u,1139u,
    196670u,1140u,1079u,393281u,1013u,1142u,1014u,1031u,1141u,196670u,
    1142u,1081u,393281u,1013u,1144u,1014u,1031u,1143u,196670u,1144u,
    1083u,393281u,1013u,1146u,1014u,1031u,1145u,196670u,1146u,1085u,
    393281u,1013u,1148u,1014u,1031u,1147u,196670u,1148u,1087u,393281u,
    1013u,1150u,1014u,1031u,1149u,196670u,1150u,1089u,393281u,1013u,
    1152u,1014u,1031u,1151u,196670u,1152u,1091u,131321u,1029u,131320u,
    1029u,131321u,1030u,131320u,1030u,
};
inline constexpr std::uint32_t sparse_interface_types[]={
    262187u,6u,1162u,1u,262172u,1163u,286u,1162u,262176u,1164u,
    0u,1163u,
};
inline constexpr std::uint32_t sparse_interface_variable[]={
    262203u,1164u,289u,0u,
};
inline constexpr std::uint32_t sparse_output_bound=1165u;
} // namespace detail
enum class Election : std::uint8_t {Pixel0,FirstCovered,Sparse32};
struct Artifact {
    std::array<std::uint32_t,frame_pixel_ssbo_clone::ProbeInterface::MaximumBytes/4> words{};
    std::size_t bytes=0;
    frame_pixel_ssbo_clone::ProbeInterface profile{};
};
inline bool transform(const frame_pixel_ssbo_clone::ShaderDefinition& original,Election election,
                      Artifact& out,frame_pixel_ssbo_clone::Refusal& why) noexcept {
    namespace clone=frame_pixel_ssbo_clone;
    out={};why={};
    if(election!=Election::Pixel0 && election!=Election::FirstCovered && election!=Election::Sparse32)
        return clone::refuse(why,clone::Reason::ShaderProfile);
    const bool firstCovered=election==Election::FirstCovered,sparse=election==Election::Sparse32;
    if(!original.complete || original.bytes!=6620)return clone::refuse(why,clone::Reason::ShaderProfile);
    VkShaderModuleCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize=original.bytes;info.pCode=original.words.data();clone::ShaderDefinition checked;
    if(!clone::captureShader(info,clone::ShaderRole::Fragment,checked,why))return false;
    const auto* input=original.words.data();
    if(input[3]!=detail::original_bound)return clone::refuse(why,clone::Reason::ShaderProfile);
    std::size_t used=0;
    const auto append=[&](const std::uint32_t* words,std::size_t count) noexcept {
        if(count>out.words.size()-used)return false;
        std::memcpy(out.words.data()+used,words,count*4);used+=count;return true;
    };
    if(!append(input,5))return clone::refuse(why,clone::Reason::Bounds);
    out.words[3]=sparse?detail::sparse_output_bound:detail::output_bound;
    for(std::size_t at=5;at<original.bytes/4;){
        const auto count=input[at]>>16;
        if(!count || count>original.bytes/4-at)return clone::refuse(why,clone::Reason::ShaderProfile);
        bool added=true;
        if(at==detail::insertion_offsets[0])added=append(detail::annotations,sizeof(detail::annotations)/4);
        if(at==detail::insertion_offsets[1])added=sparse?append(detail::sparse_globals,sizeof(detail::sparse_globals)/4):append(detail::globals,sizeof(detail::globals)/4);
        if(at==detail::insertion_offsets[2])added=sparse?append(detail::sparse_body,sizeof(detail::sparse_body)/4):
            (firstCovered?append(detail::first_body,sizeof(detail::first_body)/4):append(detail::pixel_body,sizeof(detail::pixel_body)/4));
        if(!added)return clone::refuse(why,clone::Reason::Bounds);
        if(at==detail::variable_offset){
            const bool types=sparse?append(detail::sparse_interface_types,sizeof(detail::sparse_interface_types)/4):append(detail::interface_types,sizeof(detail::interface_types)/4);
            const bool variable=sparse?append(detail::sparse_interface_variable,sizeof(detail::sparse_interface_variable)/4):append(detail::interface_variable,sizeof(detail::interface_variable)/4);
            if(!types || !variable)return clone::refuse(why,clone::Reason::Bounds);
        }else if(!append(input+at,count))return clone::refuse(why,clone::Reason::Bounds);
        at+=count;
    }
    out.bytes=used*4;
    if(!clone::ProbeInterface::count1(out.words.data(),out.bytes,out.profile,why) ||
       out.profile.firstCoveredInvocation()!=firstCovered || out.profile.sparseGrid32()!=sparse){out={};return clone::refuse(why,clone::Reason::ShaderPin);}
    return true;
}
// Preserve the two original canonical artifacts and their existing call sites.
inline bool transform(const frame_pixel_ssbo_clone::ShaderDefinition& original,bool firstCovered,
                      Artifact& out,frame_pixel_ssbo_clone::Refusal& why) noexcept {
    return transform(original,firstCovered?Election::FirstCovered:Election::Pixel0,out,why);
}
} // namespace frame_pixel_ssbo_profile
