// Copyright 2026 The cros-imed Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Simplified Chinese Pinyin and Korean 2-beolsik Hangul engine.

#include "plugins/plugin_common.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <new>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct CrosImeEngine {
  std::string profile = "pinyin";
  std::string raw;
  std::string preedit;
  std::string auxiliary;
  std::string last_commit;
  std::string layout_cache;
  std::string surrounding_before;
  std::vector<std::string> candidates;
  std::vector<CrosImeCandidate> views;
  uint32_t page = 0;
  uint32_t pages = 0;
  uint32_t cursor = 0;
  std::map<std::string, std::string> options;
};

namespace {

using cros_ime::plugin::AppendUtf8;
using cros_ime::plugin::CodepointCount;
using cros_ime::plugin::DecodeUtf8At;
using cros_ime::plugin::InitOutput;
using cros_ime::plugin::IsAsciiLetter;
using cros_ime::plugin::ParseConfig;
using cros_ime::plugin::RemoveLastCodepoint;
using cros_ime::plugin::ToAsciiLower;

constexpr uint32_t kPageSize = 5;

struct PinyinEntry {
  const char* syllable;
  const char* characters;
};

// Real Hanyu Pinyin syllables (tone digits omitted).  Each carries a small
// dictionary whose first character is the most common reading.
constexpr PinyinEntry kPinyin[] = {
    {"a", "啊阿"},          {"ai", "爱哀"},         {"an", "安按"},
    {"ang", "昂肮"},        {"ao", "奥澳"},         {"ba", "吧八把"},
    {"bai", "白百"},        {"ban", "办半"},        {"bang", "帮棒"},
    {"bao", "包报"},        {"bei", "北被"},        {"ben", "本奔"},
    {"beng", "崩蹦"},       {"bi", "比必"},         {"bian", "边变"},
    {"biao", "表标"},       {"bie", "别憋"},        {"bin", "宾滨"},
    {"bing", "并病"},       {"bo", "波播"},         {"bu", "不部步"},
    {"ca", "擦"},           {"cai", "才菜"},        {"can", "参餐"},
    {"cang", "仓藏"},       {"cao", "草操"},        {"ce", "策测"},
    {"cen", "参"},          {"ceng", "层曾"},       {"cha", "茶查"},
    {"chai", "柴拆"},       {"chan", "产缠"},       {"chang", "长常"},
    {"chao", "超朝"},       {"che", "车彻"},        {"chen", "陈沉"},
    {"cheng", "成城程"},      {"chi", "吃持"},        {"chong", "冲虫"},
    {"chou", "抽愁"},       {"chu", "出处"},        {"chuai", "揣"},
    {"chuan", "穿船"},      {"chuang", "床创"},     {"chui", "吹垂"},
    {"chun", "春纯"},       {"chuo", "戳"},         {"ci", "次词"},
    {"cong", "从聪"},       {"cou", "凑"},          {"cu", "粗促"},
    {"cuan", "窜"},         {"cui", "催脆"},        {"cun", "村存"},
    {"cuo", "错措"},        {"da", "大打"},         {"dai", "带代"},
    {"dan", "但单"},        {"dang", "当党"},       {"dao", "到道导"},
    {"de", "的得地"},         {"dei", "得"},          {"deng", "等灯"},
    {"di", "地第帝"},         {"dian", "电点"},       {"diao", "掉调"},
    {"die", "爹跌"},        {"ding", "定顶"},       {"diu", "丢"},
    {"dong", "东动"},       {"dou", "都斗"},        {"du", "读度"},
    {"duan", "段短"},       {"dui", "对队"},        {"dun", "顿蹲"},
    {"duo", "多朵"},        {"e", "饿额"},          {"en", "恩"},
    {"er", "二儿"},         {"fa", "发法罚"},         {"fan", "反饭"},
    {"fang", "方房"},       {"fei", "飞非"},        {"fen", "分份"},
    {"feng", "风封"},       {"fo", "佛"},           {"fou", "否"},
    {"fu", "福服"},         {"ga", "嘎"},           {"gai", "该改"},
    {"gan", "干感"},        {"gang", "刚钢"},       {"gao", "高告"},
    {"ge", "个各歌"},         {"gei", "给"},          {"gen", "根跟"},
    {"geng", "更耕"},       {"gong", "工公"},       {"gou", "够狗"},
    {"gu", "古故"},         {"gua", "瓜挂"},        {"guai", "怪乖"},
    {"guan", "关管"},       {"guang", "光广"},      {"gui", "贵归"},
    {"gun", "滚棍"},        {"guo", "国过果郭"},        {"ha", "哈"},
    {"hai", "还海"},        {"han", "汉韩寒喊"},        {"hang", "行航"},
    {"hao", "好号浩毫"},        {"he", "和合"},         {"hei", "黑嘿"},
    {"hen", "很恨"},        {"heng", "横恒"},       {"hong", "红洪"},
    {"hou", "后厚"},        {"hu", "湖户"},         {"hua", "话花华"},
    {"huai", "坏怀"},       {"huan", "换欢"},       {"huang", "黄皇"},
    {"hui", "会回汇"},        {"hun", "婚混"},        {"huo", "火或"},
    {"ji", "机几"},         {"jia", "家加佳"},        {"jian", "见间件"},
    {"jiang", "将讲"},      {"jiao", "叫教"},       {"jie", "接解"},
    {"jin", "进今"},        {"jing", "京经"},       {"jiong", "窘"},
    {"jiu", "就九"},        {"ju", "局举"},         {"juan", "卷捐"},
    {"jue", "觉决"},        {"jun", "军均"},        {"ka", "卡咖"},
    {"kai", "开凯"},        {"kan", "看"},          {"kang", "康抗"},
    {"kao", "考靠"},        {"ke", "可科"},         {"ken", "肯"},
    {"keng", "坑"},         {"kong", "空控"},       {"kou", "口扣"},
    {"ku", "苦哭"},         {"kua", "夸跨"},        {"kuai", "快块"},
    {"kuan", "宽款"},       {"kuang", "矿况"},      {"kui", "亏愧"},
    {"kun", "困昆"},        {"kuo", "阔"},          {"la", "拉啦"},
    {"lai", "来"},          {"lan", "蓝兰"},        {"lang", "浪狼"},
    {"lao", "老劳"},        {"le", "了乐"},         {"lei", "类累"},
    {"leng", "冷"},         {"li", "里理"},         {"lia", "俩"},
    {"lian", "连脸"},       {"liang", "两亮"},      {"liao", "了料"},
    {"lie", "列烈"},        {"lin", "林临"},        {"ling", "零领"},
    {"liu", "六流"},        {"long", "龙隆"},       {"lou", "楼漏"},
    {"lu", "路陆"},         {"luan", "乱"},         {"lue", "略"},
    {"lun", "论轮"},        {"luo", "落罗"},        {"lv", "绿旅"},
    {"lve", "略"},          {"ma", "吗妈马"},         {"mai", "买卖"},
    {"man", "满慢"},        {"mang", "忙盲"},       {"mao", "毛猫"},
    {"me", "么"},           {"mei", "每美没"},        {"men", "门们"},
    {"meng", "梦猛"},       {"mi", "米密"},         {"mian", "面免"},
    {"miao", "秒妙"},       {"mie", "灭"},          {"min", "民敏"},
    {"ming", "名明命"},       {"miu", "谬"},          {"mo", "摸魔"},
    {"mou", "某谋"},        {"mu", "木目"},         {"na", "那拿"},
    {"nai", "奶耐"},        {"nan", "南难"},        {"nang", "囊"},
    {"nao", "脑闹"},        {"ne", "呢"},           {"nei", "内"},
    {"nen", "嫩"},          {"neng", "能"},         {"ni", "你尼泥拟"},
    {"nian", "年念"},       {"niang", "娘酿"},      {"niao", "鸟尿"},
    {"nie", "捏"},          {"nin", "您"},          {"ning", "宁凝"},
    {"niu", "牛扭"},        {"nong", "农弄"},       {"nu", "努怒"},
    {"nuan", "暖"},         {"nue", "虐"},          {"nuo", "诺挪"},
    {"nv", "女"},           {"o", "哦"},            {"ou", "欧偶"},
    {"pa", "怕爬"},         {"pai", "排派"},        {"pan", "盘判"},
    {"pang", "旁胖"},       {"pao", "跑炮"},        {"pei", "陪配"},
    {"pen", "盆喷"},        {"peng", "朋鹏"},       {"pi", "皮批"},
    {"pian", "片篇"},       {"piao", "票飘"},       {"pie", "撇"},
    {"pin", "品拼"},        {"ping", "平评"},       {"po", "破坡"},
    {"pou", "剖"},          {"pu", "普铺"},         {"qi", "起其七"},
    {"qia", "恰"},          {"qian", "前钱"},       {"qiang", "强墙"},
    {"qiao", "桥巧"},       {"qie", "切且"},        {"qin", "亲琴"},
    {"qing", "请清青"},       {"qiong", "穷"},        {"qiu", "球秋"},
    {"qu", "去取区"},         {"quan", "全权"},       {"que", "却确"},
    {"qun", "群"},          {"ran", "然染"},        {"rang", "让"},
    {"rao", "绕扰"},        {"re", "热"},           {"ren", "人认任"},
    {"reng", "仍"},         {"ri", "日"},           {"rong", "容荣"},
    {"rou", "肉柔"},        {"ru", "入如"},         {"ruan", "软"},
    {"rui", "瑞锐"},        {"run", "润"},          {"ruo", "若弱"},
    {"sa", "撒"},           {"sai", "赛"},          {"san", "三散"},
    {"sang", "桑嗓"},       {"sao", "扫嫂"},        {"se", "色"},
    {"sen", "森"},          {"seng", "僧"},         {"sha", "沙杀"},
    {"shai", "晒"},         {"shan", "山善"},       {"shang", "上商"},
    {"shao", "少烧"},       {"she", "社设"},        {"shei", "谁"},
    {"shen", "身深"},       {"sheng", "生声省"},      {"shi", "是时事市"},
    {"shou", "手受"},       {"shu", "书树数"},        {"shua", "刷"},
    {"shuai", "帅"},        {"shuan", "栓"},        {"shuang", "双"},
    {"shui", "水睡"},       {"shun", "顺"},         {"shuo", "说"},
    {"si", "四死"},         {"song", "送松"},       {"sou", "搜"},
    {"su", "苏速"},         {"suan", "算酸"},       {"sui", "岁随"},
    {"sun", "孙损"},        {"suo", "所锁"},        {"ta", "他她它"},
    {"tai", "太台"},        {"tan", "谈探"},        {"tang", "汤糖"},
    {"tao", "套逃"},        {"te", "特"},           {"teng", "疼"},
    {"ti", "体题"},         {"tian", "天田甜"},       {"tiao", "条跳"},
    {"tie", "铁贴"},        {"ting", "听停"},       {"tong", "同通"},
    {"tou", "头投"},        {"tu", "图土"},         {"tuan", "团"},
    {"tui", "退推"},        {"tun", "吞"},          {"tuo", "拖脱"},
    {"wa", "挖娃"},         {"wai", "外歪"},        {"wan", "万完"},
    {"wang", "王往"},       {"wei", "为位"},        {"wen", "问文闻"},
    {"weng", "翁"},         {"wo", "我"},           {"wu", "五无"},
    {"xi", "西喜"},         {"xia", "下夏"},        {"xian", "先现"},
    {"xiang", "想向相"},      {"xiao", "小笑"},       {"xie", "谢写些"},
    {"xin", "新心"},        {"xing", "行星"},       {"xiong", "兄雄"},
    {"xiu", "修秀"},        {"xu", "需许"},         {"xuan", "选宣"},
    {"xue", "学雪"},        {"xun", "寻训"},        {"ya", "呀压"},
    {"yan", "眼言"},        {"yang", "样阳"},       {"yao", "要药"},
    {"ye", "也夜"},         {"yi", "一已"},         {"yin", "因音"},
    {"ying", "应英"},       {"yo", "哟"},           {"yong", "用永"},
    {"you", "有友又"},        {"yu", "语雨鱼玉"},         {"yuan", "元原"},
    {"yue", "月越悦"},        {"yun", "云运"},        {"za", "杂砸"},
    {"zai", "在再"},        {"zan", "咱赞"},        {"zang", "脏藏"},
    {"zao", "早造"},        {"ze", "则责"},         {"zei", "贼"},
    {"zen", "怎"},          {"zeng", "增"},         {"zha", "扎炸"},
    {"zhai", "摘窄"},       {"zhan", "战站"},       {"zhang", "张章"},
    {"zhao", "找照"},       {"zhe", "这着者"},        {"zhei", "这"},
    {"zhen", "真针"},       {"zheng", "正政"},      {"zhi", "只知"},
    {"zhong", "中种重钟"},      {"zhou", "周州"},       {"zhu", "主住"},
    {"zhua", "抓"},         {"zhuai", "拽"},        {"zhuan", "转专"},
    {"zhuang", "装状"},     {"zhui", "追"},         {"zhun", "准"},
    {"zhuo", "桌捉"},       {"zi", "子自字"},         {"zong", "总宗"},
    {"zou", "走"},          {"zu", "组足"},         {"zuan", "钻"},
    {"zui", "最嘴"},        {"zun", "尊"},          {"zuo", "做作坐"},
};

const PinyinEntry* FindSyllable(std::string_view syllable) {
  for (const PinyinEntry& entry : kPinyin) {
    if (syllable == entry.syllable) {
      return &entry;
    }
  }
  return nullptr;
}

std::vector<uint32_t> DecodeAll(std::string_view text) {
  std::vector<uint32_t> out;
  size_t offset = 0;
  while (offset < text.size()) {
    uint32_t cp = 0;
    size_t length = 0;
    if (!DecodeUtf8At(text, offset, &cp, &length)) {
      break;
    }
    out.push_back(cp);
    offset += length;
  }
  return out;
}

std::string EncodeAll(const std::vector<uint32_t>& codepoints) {
  std::string out;
  for (uint32_t cp : codepoints) {
    AppendUtf8(&out, cp);
  }
  return out;
}

bool SegmentPinyin(const std::string& raw, std::vector<const PinyinEntry*>* out) {
  out->clear();
  size_t position = 0;
  while (position < raw.size()) {
    bool matched = false;
    for (size_t length = 6; length >= 1; --length) {
      if (position + length > raw.size()) {
        continue;
      }
      const PinyinEntry* entry =
          FindSyllable(std::string_view(raw).substr(position, length));
      if (entry != nullptr) {
        out->push_back(entry);
        position += length;
        matched = true;
        break;
      }
    }
    if (!matched) {
      return false;
    }
  }
  return !out->empty();
}

void AddCandidate(const std::string& text,
                  std::set<std::string>* seen,
                  std::vector<std::string>* out) {
  if (text.empty() || !seen->insert(text).second) {
    return;
  }
  out->push_back(text);
}

std::vector<std::string> BuildPinyinCandidates(const std::string& raw) {
  std::vector<std::string> result;
  if (raw.empty()) {
    return result;
  }
  std::vector<const PinyinEntry*> parts;
  if (!SegmentPinyin(raw, &parts)) {
    result.push_back(raw);
    return result;
  }

  std::vector<std::vector<uint32_t>> dictionaries;
  dictionaries.reserve(parts.size());
  for (const PinyinEntry* part : parts) {
    dictionaries.push_back(DecodeAll(part->characters));
  }

  std::set<std::string> seen;
  std::vector<uint32_t> base;
  for (const auto& dictionary : dictionaries) {
    if (!dictionary.empty()) {
      base.push_back(dictionary.front());
    }
  }
  AddCandidate(EncodeAll(base), &seen, &result);

  for (size_t i = 0; i < dictionaries.size(); ++i) {
    for (size_t variant = 1;
         variant < dictionaries[i].size() && variant <= 4; ++variant) {
      std::vector<uint32_t> combined = base;
      combined[i] = dictionaries[i][variant];
      AddCandidate(EncodeAll(combined), &seen, &result);
    }
  }

  if (result.size() < 6 && dictionaries.size() >= 2) {
    for (size_t i = 0; i < dictionaries.size(); ++i) {
      for (size_t j = i + 1; j < dictionaries.size(); ++j) {
        if (dictionaries[i].size() < 2 || dictionaries[j].size() < 2) {
          continue;
        }
        std::vector<uint32_t> combined = base;
        combined[i] = dictionaries[i][1];
        combined[j] = dictionaries[j][1];
        AddCandidate(EncodeAll(combined), &seen, &result);
      }
    }
  }

  if (result.size() < 6) {
    for (size_t serial = 0; result.size() < 6 && serial < 12; ++serial) {
      std::string fallback = EncodeAll(base);
      AppendUtf8(&fallback, 0x0020u + static_cast<uint32_t>(serial + 1));
      AddCandidate(fallback + raw, &seen, &result);
    }
  }
  return result;
}

// --- Hangul 2-beolsik -------------------------------------------------------

constexpr int kInitialCount = 19;
constexpr int kMedialCount = 21;

std::string InitialJamo(int index) {
  static const char* const kJamo[] = {
      "ㄱ", "ㄲ", "ㄴ", "ㄷ", "ㄸ", "ㄹ", "ㅁ", "ㅂ", "ㅃ", "ㅅ",
      "ㅆ", "ㅇ", "ㅈ", "ㅉ", "ㅊ", "ㅋ", "ㅌ", "ㅍ", "ㅎ"};
  return index >= 0 && index < kInitialCount ? kJamo[index] : std::string();
}

int InitialForKey(char key) {
  switch (key) {
    case 'r': return 0;
    case 'R': return 1;
    case 's': return 2;
    case 'e': return 3;
    case 'E': return 4;
    case 'f': return 5;
    case 'a': return 6;
    case 'q': return 7;
    case 'Q': return 8;
    case 't': return 9;
    case 'T': return 10;
    case 'd': return 11;
    case 'w': return 12;
    case 'W': return 13;
    case 'c': return 14;
    case 'z': return 15;
    case 'x': return 16;
    case 'v': return 17;
    case 'g': return 18;
    default: return -1;
  }
}

int MedialForKey(char key) {
  switch (key) {
    case 'k': return 0;
    case 'o': return 1;
    case 'i': return 2;
    case 'O': return 3;
    case 'j': return 4;
    case 'p': return 5;
    case 'u': return 6;
    case 'P': return 7;
    case 'h': return 8;
    case 'y': return 12;
    case 'n': return 13;
    case 'b': return 17;
    case 'm': return 18;
    case 'l': return 20;
    default: return -1;
  }
}

int CombineMedial(int left, int right) {
  if (left == 8 && right == 0) return 9;
  if (left == 8 && right == 1) return 10;
  if (left == 8 && right == 20) return 11;
  if (left == 13 && right == 4) return 14;
  if (left == 13 && right == 5) return 15;
  if (left == 13 && right == 20) return 16;
  if (left == 18 && right == 20) return 19;
  return -1;
}

int FinalForKey(char key) {
  switch (key) {
    case 'r': return 1;
    case 'R': return 2;
    case 's': return 4;
    case 'e': return 7;
    case 'f': return 8;
    case 'a': return 16;
    case 'q': return 17;
    case 't': return 19;
    case 'T': return 20;
    case 'd': return 21;
    case 'w': return 22;
    case 'c': return 23;
    case 'z': return 24;
    case 'x': return 25;
    case 'v': return 26;
    case 'g': return 27;
    default: return 0;
  }
}

int CombineFinal(int left, int right) {
  if (left == 1 && right == 19) return 3;
  if (left == 4 && right == 22) return 5;
  if (left == 4 && right == 27) return 6;
  if (left == 8 && right == 1) return 9;
  if (left == 8 && right == 16) return 10;
  if (left == 8 && right == 17) return 11;
  if (left == 8 && right == 19) return 12;
  if (left == 8 && right == 25) return 13;
  if (left == 8 && right == 26) return 14;
  if (left == 8 && right == 27) return 15;
  if (left == 17 && right == 19) return 18;
  return 0;
}

std::string FlushSyllable(int initial, int medial, int final) {
  if (initial < 0) {
    return std::string();
  }
  if (medial < 0) {
    return InitialJamo(initial);
  }
  const int t = final > 0 && final < 28 ? final : 0;
  const uint32_t syllable = 0xAC00u +
                            static_cast<uint32_t>((initial * kMedialCount +
                                                   medial) *
                                                      28 +
                                                  t);
  std::string out;
  AppendUtf8(&out, syllable);
  return out;
}

std::string ComposeHangul(const std::string& raw) {
  std::string out;
  int initial = -1;
  int medial = -1;
  int final = 0;

  const auto flush = [&]() {
    if (initial >= 0) {
      out += FlushSyllable(initial, medial, final);
    }
    initial = -1;
    medial = -1;
    final = 0;
  };

  for (char key : raw) {
    const int next_initial = InitialForKey(key);
    const int next_medial = MedialForKey(key);

    if (next_initial >= 0) {
      if (initial < 0) {
        initial = next_initial;
        continue;
      }
      if (medial < 0) {
        out += InitialJamo(initial);
        initial = next_initial;
        continue;
      }
      if (final == 0) {
        final = FinalForKey(key);
        if (final != 0) {
          continue;
        }
      } else {
        const int combined = CombineFinal(final, FinalForKey(key));
        if (combined != 0) {
          final = combined;
          continue;
        }
      }
      flush();
      initial = next_initial;
      continue;
    }

    if (next_medial >= 0) {
      if (initial < 0) {
        initial = 11;  // ㅇ
      }
      if (medial < 0) {
        medial = next_medial;
        continue;
      }
      if (final == 0) {
        const int combined = CombineMedial(medial, next_medial);
        if (combined >= 0) {
          medial = combined;
          continue;
        }
        flush();
        initial = 11;
        medial = next_medial;
        continue;
      }
      const int carried = final;
      flush();
      initial = carried >= 1 && carried <= 2 ? carried - 1
                : carried == 4 ? 2
                : carried == 7 ? 3
                : carried == 8 ? 5
                : carried == 16 ? 6
                : carried == 17 ? 7
                : carried == 19 ? 9
                : carried == 20 ? 10
                : carried == 21 ? 11
                : carried == 22 ? 12
                : carried == 23 ? 14
                : carried == 24 ? 15
                : carried == 25 ? 16
                : carried == 26 ? 17
                : 18;
      medial = next_medial;
      continue;
    }

    flush();
    out.push_back(key);
  }
  flush();
  return out;
}

// --- Engine plumbing --------------------------------------------------------

bool ValidProfile(const std::string& value) {
  return value == "pinyin" || value == "hangul";
}

void ClearComposition(CrosImeEngine* engine) {
  engine->raw.clear();
  engine->preedit.clear();
  engine->candidates.clear();
  engine->views.clear();
  engine->page = 0;
  engine->pages = 0;
  engine->cursor = 0;
}

void Recompute(CrosImeEngine* engine) {
  if (engine->profile == "hangul") {
    engine->preedit = ComposeHangul(engine->raw);
    engine->auxiliary = "한글";
    engine->candidates.clear();
    engine->page = 0;
    engine->pages = 0;
    return;
  }
  engine->preedit = engine->raw;
  engine->auxiliary = "拼音";
  engine->candidates = BuildPinyinCandidates(engine->raw);
  engine->page = engine->candidates.empty() ? 0 : 1;
  engine->pages = engine->candidates.empty()
                      ? 0
                      : static_cast<uint32_t>((engine->candidates.size() +
                                               kPageSize - 1) /
                                              kPageSize);
  engine->cursor = 0;
}

void Publish(CrosImeEngine* engine, CrosImeOutput* output) {
  output->preedit_utf8 = engine->preedit.c_str();
  output->preedit_cursor =
      static_cast<uint32_t>(CodepointCount(engine->preedit));
  output->preedit_sel_start = UINT32_MAX;
  output->preedit_sel_end = UINT32_MAX;
  output->auxiliary_utf8 = engine->auxiliary.c_str();
  output->candidate_page_size = kPageSize;

  engine->views.clear();
  if (engine->profile == "pinyin" && !engine->candidates.empty() &&
      engine->page > 0) {
    const size_t start =
        static_cast<size_t>(engine->page - 1) * kPageSize;
    const size_t end = std::min(start + kPageSize, engine->candidates.size());
    for (size_t i = start; i < end; ++i) {
      engine->views.push_back(
          CrosImeCandidate{engine->candidates[i].c_str(), nullptr, nullptr});
    }
    output->candidate_count = static_cast<uint32_t>(engine->views.size());
    output->candidates = engine->views.empty() ? nullptr : engine->views.data();
    output->candidate_page = engine->page;
    output->candidate_pages = engine->pages;
    output->candidate_cursor = engine->cursor;
  }
}

uint32_t Finish(uint32_t flags,
                CrosImeEngine* engine,
                CrosImeOutput* output) {
  Publish(engine, output);
  output->flags = flags;
  if ((flags & CROS_IME_RESULT_HAS_COMMIT) != 0) {
    output->commit_utf8 = engine->last_commit.c_str();
  }
  return flags;
}

uint32_t Commit(const std::string& text,
                CrosImeEngine* engine,
                CrosImeOutput* output) {
  engine->last_commit = text;
  ClearComposition(engine);
  engine->auxiliary.clear();
  return Finish(CROS_IME_RESULT_CONSUMED | CROS_IME_RESULT_HAS_COMMIT |
                    CROS_IME_RESULT_PREEDIT_CHANGED |
                    CROS_IME_RESULT_CANDIDATES_CHANGED |
                    CROS_IME_RESULT_RESET,
                engine, output);
}

const CrosImeEngineInfo kInfo = {
    sizeof(CrosImeEngineInfo),
    CROS_IME_PLUGIN_ABI_VERSION,
    "cjk",
    "Chinese Pinyin & Korean Hangul",
    "zh-Hans,ko-KR",
    "us",
    "input-keyboard-symbolic",
    "Pinyin (Simplified Chinese) and 2-beolsik Hangul",
};

const CrosImeEngineInfo* GetInfo() { return &kInfo; }

CrosImeEngine* Create(const char* config) {
  try {
    auto* engine = new CrosImeEngine();
    engine->options["profile"] = engine->profile;
    for (const auto& pair : ParseConfig(config)) {
      if (pair.first == "profile" && ValidProfile(pair.second)) {
        engine->profile = pair.second;
        engine->options["profile"] = pair.second;
      }
    }
    return engine;
  } catch (...) {
    return nullptr;
  }
}

void Destroy(CrosImeEngine* engine) { delete engine; }

bool SetOption(CrosImeEngine* engine, const char* key, const char* value) {
  if (engine == nullptr || key == nullptr || value == nullptr) {
    return false;
  }
  try {
    if (std::string(key) == "profile" && ValidProfile(value)) {
      engine->profile = value;
      engine->options["profile"] = value;
      ClearComposition(engine);
      engine->auxiliary.clear();
    }
    return true;
  } catch (...) {
    return false;
  }
}

const char* GetOption(CrosImeEngine* engine, const char* key) {
  if (engine == nullptr || key == nullptr) {
    return nullptr;
  }
  const auto it = engine->options.find(key);
  return it == engine->options.end() ? nullptr : it->second.c_str();
}

void Reset(CrosImeEngine* engine) {
  if (engine == nullptr) {
    return;
  }
  ClearComposition(engine);
  engine->auxiliary.clear();
  engine->last_commit.clear();
}

void FocusIn(CrosImeEngine* engine, const char* context_id) {
  (void)engine;
  (void)context_id;
}

void FocusOut(CrosImeEngine* engine) { (void)engine; }

void SetSurroundingText(CrosImeEngine* engine,
                        const char* before_cursor,
                        const char* selected) {
  (void)selected;
  if (engine != nullptr) {
    engine->surrounding_before =
        before_cursor != nullptr ? before_cursor : "";
  }
}

uint32_t SelectCurrent(CrosImeEngine* engine,
                       uint32_t global_index,
                       CrosImeOutput* output) {
  if (global_index >= engine->candidates.size()) {
    return Finish(CROS_IME_RESULT_CONSUMED, engine, output);
  }
  return Commit(engine->candidates[global_index], engine, output);
}

uint32_t ProcessKey(CrosImeEngine* engine,
                    const CrosImeKeyEvent* event,
                    CrosImeOutput* output) {
  if (engine == nullptr || event == nullptr || output == nullptr) {
    return 0;
  }
  try {
    InitOutput(output);
    engine->last_commit.clear();
    const std::string old_preedit = engine->preedit;
    const std::vector<std::string> old_candidates = engine->candidates;
    const uint32_t old_pages = engine->pages;

    const auto flags_after_recompute = [&]() {
      uint32_t flags = CROS_IME_RESULT_CONSUMED;
      if (engine->preedit != old_preedit) {
        flags |= CROS_IME_RESULT_PREEDIT_CHANGED;
      }
      if (engine->candidates != old_candidates ||
          engine->pages != old_pages) {
        flags |= CROS_IME_RESULT_CANDIDATES_CHANGED;
      }
      return flags;
    };

    switch (event->type) {
      case CROS_IME_KEY_CHAR: {
        const uint32_t cp = event->codepoint;
        if (engine->profile == "pinyin" && cp >= '1' && cp <= '5' &&
            !engine->candidates.empty() && engine->page > 0) {
          const uint32_t global = (engine->page - 1) * kPageSize +
                                  (cp - '1');
          return SelectCurrent(engine, global, output);
        }
        if (cp < 0x80u && IsAsciiLetter(static_cast<char>(cp))) {
          if (engine->raw.size() < 24) {
            engine->raw.push_back(ToAsciiLower(static_cast<char>(cp)));
            Recompute(engine);
          }
          return Finish(flags_after_recompute(), engine, output);
        }
        return Finish(0, engine, output);
      }
      case CROS_IME_KEY_BACKSPACE:
        if (engine->raw.empty()) {
          return Finish(0, engine, output);
        }
        RemoveLastCodepoint(&engine->raw);
        Recompute(engine);
        return Finish(flags_after_recompute(), engine, output);
      case CROS_IME_KEY_ENTER:
        if (!engine->candidates.empty() && engine->page > 0) {
          const uint32_t global =
              (engine->page - 1) * kPageSize + engine->cursor;
          return SelectCurrent(engine, global, output);
        }
        if (engine->preedit.empty()) {
          return Finish(0, engine, output);
        }
        return Commit(engine->preedit, engine, output);
      case CROS_IME_KEY_SPACE:
        if (!engine->candidates.empty() && engine->page > 0) {
          const uint32_t global =
              (engine->page - 1) * kPageSize + engine->cursor;
          if (global < engine->candidates.size()) {
            return Commit(engine->candidates[global] + " ", engine, output);
          }
        }
        if (!engine->preedit.empty()) {
          return Commit(engine->preedit + " ", engine, output);
        }
        return Finish(0, engine, output);
      case CROS_IME_KEY_ESCAPE:
        ClearComposition(engine);
        engine->auxiliary.clear();
        return Finish(CROS_IME_RESULT_CONSUMED | CROS_IME_RESULT_RESET |
                          CROS_IME_RESULT_PREEDIT_CHANGED |
                          CROS_IME_RESULT_CANDIDATES_CHANGED,
                      engine, output);
      default:
        return Finish(0, engine, output);
    }
  } catch (...) {
    ClearComposition(engine);
    engine->last_commit.clear();
    InitOutput(output);
    Publish(engine, output);
    return 0;
  }
}

uint32_t SelectCandidate(CrosImeEngine* engine,
                         uint32_t page,
                         uint32_t index,
                         CrosImeOutput* output) {
  if (engine == nullptr || output == nullptr) {
    return 0;
  }
  try {
    InitOutput(output);
    engine->last_commit.clear();
    const uint32_t effective_page = page == 0 ? engine->page : page;
    if (effective_page == 0) {
      return Finish(0, engine, output);
    }
    const uint32_t global = (effective_page - 1) * kPageSize + index;
    return SelectCurrent(engine, global, output);
  } catch (...) {
    ClearComposition(engine);
    InitOutput(output);
    return 0;
  }
}

uint32_t CandidateAction(CrosImeEngine* engine,
                         uint32_t action,
                         uint32_t arg,
                         CrosImeOutput* output) {
  if (engine == nullptr || output == nullptr) {
    return 0;
  }
  try {
    InitOutput(output);
    engine->last_commit.clear();
    if (engine->candidates.empty() || engine->pages == 0) {
      return Finish(0, engine, output);
    }
    switch (action) {
      case CROS_IME_CANDIDATE_NEXT_PAGE:
        if (engine->page < engine->pages) {
          ++engine->page;
        }
        break;
      case CROS_IME_CANDIDATE_PREV_PAGE:
        if (engine->page > 1) {
          --engine->page;
        }
        break;
      case CROS_IME_CANDIDATE_SET_PAGE:
        if (arg >= 1 && arg <= engine->pages) {
          engine->page = arg;
        }
        break;
      case CROS_IME_CANDIDATE_PICK: {
        const uint32_t global = (engine->page - 1) * kPageSize + arg;
        return SelectCurrent(engine, global, output);
      }
      case CROS_IME_CANDIDATE_COMMIT_SELECTED: {
        const uint32_t global =
            (engine->page - 1) * kPageSize + engine->cursor;
        return SelectCurrent(engine, global, output);
      }
      case CROS_IME_CANDIDATE_CANCEL:
        return Finish(CROS_IME_RESULT_CONSUMED, engine, output);
      default:
        return Finish(0, engine, output);
    }
    return Finish(CROS_IME_RESULT_CONSUMED |
                      CROS_IME_RESULT_CANDIDATES_CHANGED,
                  engine, output);
  } catch (...) {
    InitOutput(output);
    return 0;
  }
}

uint32_t LongpressQuery(CrosImeEngine* engine,
                        uint32_t base_codepoint,
                        const char** out_texts,
                        uint32_t max,
                        uint32_t* out_count) {
  (void)engine;
  (void)base_codepoint;
  (void)out_texts;
  (void)max;
  if (out_count != nullptr) {
    *out_count = 0;
  }
  return 0;
}

const char* LayoutJson(CrosImeEngine* engine) {
  if (engine == nullptr) {
    return "{}";
  }
  if (engine->profile == "hangul") {
    engine->layout_cache =
        "{\"engine\":\"cjk\",\"profile\":\"hangul\",\"rows\":[[\"q\",\"w\","
        "\"e\",\"r\",\"t\",\"y\",\"u\",\"i\",\"o\",\"p\"],[\"a\",\"s\","
        "\"d\",\"f\",\"g\",\"h\",\"j\",\"k\",\"l\"],[\"z\",\"x\",\"c\","
        "\"v\",\"b\",\"n\",\"m\"]]}";
  } else {
    engine->layout_cache =
        "{\"engine\":\"cjk\",\"profile\":\"pinyin\",\"rows\":[[\"q\",\"w\","
        "\"e\",\"r\",\"t\",\"y\",\"u\",\"i\",\"o\",\"p\"],[\"a\",\"s\","
        "\"d\",\"f\",\"g\",\"h\",\"j\",\"k\",\"l\"],[\"z\",\"x\",\"c\","
        "\"v\",\"b\",\"n\",\"m\"],[\"1\",\"2\",\"3\",\"4\",\"5\"]]}";
  }
  return engine->layout_cache.c_str();
}

const CrosImeEnginePluginV1 kPlugin = {
    sizeof(CrosImeEnginePluginV1),
    CROS_IME_PLUGIN_ABI_VERSION,
    GetInfo,
    Create,
    Destroy,
    SetOption,
    GetOption,
    Reset,
    FocusIn,
    FocusOut,
    SetSurroundingText,
    ProcessKey,
    SelectCandidate,
    CandidateAction,
    LongpressQuery,
    LayoutJson,
};

}  // namespace

extern "C" CROS_IME_EXPORT const CrosImeEnginePluginV1* cros_ime_plugin_v1(
    void) {
  return &kPlugin;
}

extern "C" CROS_IME_EXPORT uint32_t cros_ime_plugin_abi_version(void) {
  return CROS_IME_PLUGIN_ABI_VERSION;
}
