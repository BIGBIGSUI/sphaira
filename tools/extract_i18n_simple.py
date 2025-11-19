#!/usr/bin/env python3
"""
简化版 i18n 提取器
核心思路：模拟 C++ 编译器处理字符串字面量的方式
"""

import re
import json
from pathlib import Path
from typing import Set, Dict, List
from collections import defaultdict


def extract_string_literal(s: str) -> str:
    """提取字符串字面量的内容（去掉引号，处理转义）"""
    # 去掉首尾引号
    s = s.strip()
    if s.startswith('"') and s.endswith('"'):
        s = s[1:-1]
    elif s.startswith("'") and s.endswith("'"):
        s = s[1:-1]
    
    # 处理常见转义字符
    s = s.replace('\\n', '\n')
    s = s.replace('\\t', '\t')
    s = s.replace('\\r', '\r')
    s = s.replace('\\"', '"')
    s = s.replace("\\'", "'")
    s = s.replace('\\\\', '\\')
    
    return s


def find_i18n_keys(content: str, debug: bool = False) -> Set[str]:
    """
    从 C++ 代码中查找所有 i18n key
    
    策略：
    1. 找到所有 _i18n 标记的位置
    2. 向前回溯，找到完整的字符串（可能跨多行）
    3. 合并相邻的字符串字面量
    """
    keys = set()
    debug_keywords = ['Logs to', 'Replace hbmenu'] if debug else []
    
    # 第一步：处理C++行连接符（反斜杠+换行）
    # 这必须在删除注释之前完成，因为C++预处理器先处理行连接
    content = re.sub(r'\\\r?\n', ' ', content)
    
    if debug:
        # 检查是否包含目标字符串
        has_logs = 'Logs to' in content
        has_replace = 'Replace hbmenu' in content
        print(f"    [DEBUG] 处理行连接后: Logs to={has_logs}, Replace hbmenu={has_replace}")
    
    # 第二步：移除注释（简单处理）
    # 移除单行注释
    content = re.sub(r'//.*?$', '', content, flags=re.MULTILINE)
    # 移除块注释
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    
    if debug:
        # 再次检查
        has_logs = 'Logs to' in content
        has_replace = 'Replace hbmenu' in content
        print(f"    [DEBUG] 删除注释后: Logs to={has_logs}, Replace hbmenu={has_replace}")
    
    # 方法1: 处理 "string"_i18n 格式
    # 策略：找到 _i18n，然后向前查找所有相邻的字符串字面量
    
    # 首先找到所有 _i18n 的位置
    i18n_positions = [m.start() for m in re.finditer(r'_i18n\b', content)]
    
    if debug:
        print(f"    [DEBUG] 找到 {len(i18n_positions)} 个 _i18n 标记")
    
    for idx, pos in enumerate(i18n_positions):
        # 从 _i18n 位置向前查找字符串
        # 向前最多搜索 5000 个字符（足够长了）
        start_search = max(0, pos - 5000)
        text_before = content[start_search:pos]
        
        # 调试：检查是否包含目标字符串
        should_debug_this = debug and any(kw in text_before[-200:] for kw in debug_keywords)
        
        # 找到所有相邻的字符串字面量
        # 从后往前找：最后一个字符串必须紧邻 _i18n
        string_parts = []
        
        # 匹配字符串字面量的正则（支持转义）
        # C++字符串字面量可以包含\n但不应该包含真实的换行符（除非在源代码中被行连接符连接）
        # 由于我们已经处理了行连接符，这里匹配不包含真实换行的字符串
        string_pattern = r'"(?:[^"\\\n]|\\.)*"'
        
        # 从后往前查找
        remaining = text_before
        while True:
            # 找到最后一个字符串
            matches = list(re.finditer(string_pattern, remaining))
            if not matches:
                break
            
            last_match = matches[-1]
            string_content = last_match.group(0)
            
            # 检查这个字符串后面是否只有空白符
            after_string = remaining[last_match.end():]
            if not after_string.strip():
                # 这是一个有效的字符串部分
                string_parts.insert(0, string_content)
                # 继续向前查找
                remaining = remaining[:last_match.start()]
            else:
                # 后面有非空白字符，停止查找
                if should_debug_this:
                    print(f"    [DEBUG方法1] _i18n#{idx} at pos {pos}:")
                    print(f"      匹配到的字符串: {repr(string_content[:40])}")
                    print(f"      后面的非空白内容: {repr(after_string[:50])}")
                    print(f"      remaining末尾100字符: {repr(remaining[-100:])}")
                break
        
        if string_parts:
            # 合并所有字符串部分
            full_key = ''.join(extract_string_literal(s) for s in string_parts)
            if full_key:  # 忽略空字符串
                if debug and any(kw in full_key for kw in debug_keywords):
                    print(f"    [DEBUG方法1] _i18n#{idx} 提取键值: {repr(full_key[:60])}")
                keys.add(full_key)
        elif should_debug_this:
            print(f"    [DEBUG方法1] _i18n#{idx} at pos {pos}: 没有找到字符串部分")
    
    # 方法2: 处理 i18n::get("string") 格式
    get_pattern = r'i18n::get\s*\(\s*"((?:[^"\\]|\\.)*)"\s*\)'
    for match in re.finditer(get_pattern, content):
        key = match.group(1)
        key = extract_string_literal('"' + key + '"')
        if key:
            keys.add(key)
    
    # 方法3: 处理结构体中的字符串常量（用于延迟翻译）
    # 查找 .info = "string" 或 .title = "string" 等字段
    # 这些字符串会在运行时通过 i18n::get() 翻译
    struct_fields = ['.info', '.title']
    
    for field in struct_fields:
        # 查找字段后面的字符串
        field_pattern = re.escape(field) + r'\s*=\s*'
        
        # 找到所有匹配的位置
        for match in re.finditer(field_pattern, content):
            pos = match.end()
            
            # 从字段位置向后查找字符串
            # 查找接下来的 5000 个字符（足够长）
            text_after = content[pos:pos + 5000]
            
            # 找到所有相邻的字符串字面量，直到遇到逗号、分号或右花括号
            string_pattern = r'"(?:[^"\\]|\\.)*"'
            string_parts = []
            
            # 收集所有连续的字符串
            remaining = text_after
            while True:
                # 找第一个字符串
                match_str = re.match(r'\s*' + string_pattern, remaining)
                if not match_str:
                    break
                
                string_content = match_str.group(0).strip()
                string_parts.append(string_content)
                
                # 继续查找后续字符串
                after_str = remaining[match_str.end():]
                # 检查后面是否还有字符串（中间只能有空白）
                next_match = re.match(r'\s*' + string_pattern, after_str)
                if next_match:
                    remaining = after_str
                else:
                    # 检查是否遇到终止符
                    if re.match(r'\s*[,;}]', after_str):
                        break
                    # 如果后面还有其他内容但不是字符串，停止
                    break
            
            if string_parts:
                # 合并所有字符串部分
                full_key = ''.join(extract_string_literal(s) for s in string_parts)
                if full_key and len(full_key) > 1:  # 忽略空字符串和单字符
                    keys.add(full_key)
    
    # 方法4: 处理 GetShortTitle() 等方法中返回的字符串
    # 这些字符串会被 i18n::get() 在运行时翻译
    # 模式: return "String"; 在特定方法中（如 GetShortTitle）
    
    # 查找 GetShortTitle() 方法定义中的返回字符串
    # 支持多种C++语法：
    # 1. auto GetShortTitle() const -> const char* override { return "String"; };
    # 2. const char* GetShortTitle() const override { return "String"; }
    # 3. virtual auto GetShortTitle() const -> const char* { return "String"; }
    
    # 改进的正则：匹配从GetShortTitle到return语句之间的内容
    # 使用非贪婪匹配，确保能匹配到 { return "..." }
    short_title_pattern = r'GetShortTitle\s*\([^)]*\)\s*(?:const)?\s*(?:->[\w\s*:]+)?\s*(?:override)?\s*{\s*return\s+"([^"]+)"\s*;\s*}'
    for match in re.finditer(short_title_pattern, content):
        key = match.group(1)
        if key and len(key) > 1:  # 忽略空字符串和单字符
            keys.add(key)
    
    if debug:
        short_title_matches = list(re.finditer(short_title_pattern, content))
        print(f"    [DEBUG] 找到 {len(short_title_matches)} 个 GetShortTitle 方法")
    
    # 更通用的模式：查找被 i18n::get() 直接调用的字符串常量
    # 这个模式在前面的方法2已经处理了，但我们可以扩展它
    # 同时查找 i18n::get(variable) 中的 variable 定义
    # 例如：const char* label = "Store"; ... i18n::get(label);
    # 这需要更复杂的数据流分析，暂时跳过
    
    return keys


def scan_project(project_root: Path, verbose: bool = False, debug_file: str = None) -> Dict[str, List[str]]:
    """扫描项目，返回 key -> [文件路径列表]"""
    key_locations = defaultdict(list)
    
    source_dirs = [
        project_root / 'sphaira' / 'source',
        project_root / 'sphaira' / 'include',
    ]
    
    for source_dir in source_dirs:
        if not source_dir.exists():
            print(f"Warning: {source_dir} not found")
            continue
        
        for ext in ['.cpp', '.hpp', '.h', '.c']:
            for filepath in source_dir.rglob(f'*{ext}'):
                # 跳过 build 目录
                if 'build' in filepath.parts:
                    continue
                
                # 检查是否是调试文件
                is_debug = debug_file and filepath.name == debug_file
                
                if verbose or is_debug:
                    print(f"Scanning: {filepath.relative_to(project_root)}")
                
                try:
                    content = filepath.read_text(encoding='utf-8', errors='ignore')
                    keys = find_i18n_keys(content, debug=is_debug)
                    
                    if is_debug:
                        print(f"  Found {len(keys)} keys in {filepath.name}")
                        debug_keywords = ['Logs', 'Replace hbmenu', 'config', 'Logging']
                        matching_keys = [k for k in sorted(keys) if any(kw in k for kw in debug_keywords)]
                        print(f"  Matching debug keywords: {len(matching_keys)}")
                        for key in matching_keys:
                            preview = key[:70] + '...' if len(key) > 70 else key
                            print(f"    - {repr(preview)}")
                    
                    for key in keys:
                        rel_path = str(filepath.relative_to(project_root))
                        key_locations[key].append(rel_path)
                        
                        if verbose and len(key) > 80:
                            preview = key[:80] + '...'
                            print(f"  Found: {preview}")
                
                except Exception as e:
                    print(f"Error reading {filepath}: {e}")
    
    return key_locations


def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='Simple i18n key extractor')
    parser.add_argument('--project-root', type=Path, default=Path.cwd(), help='Project root')
    parser.add_argument('--output', type=Path, help='Output JSON file')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    parser.add_argument('--compare', type=Path, help='Compare with existing en.json')
    parser.add_argument('--debug-file', type=str, help='Debug specific file (e.g., app.cpp)')
    
    args = parser.parse_args()
    
    print(f"Scanning project: {args.project_root}")
    
    # 扫描项目
    key_locations = scan_project(args.project_root, args.verbose, args.debug_file)
    
    print(f"\n{'='*60}")
    print(f"Found {len(key_locations)} unique keys")
    print(f"{'='*60}")
    
    # 与现有翻译对比
    if args.compare:
        try:
            with open(args.compare, 'r', encoding='utf-8') as f:
                existing = json.load(f)
            
            found_keys = set(key_locations.keys())
            existing_keys = set(existing.keys())
            
            missing = found_keys - existing_keys
            extra = existing_keys - found_keys
            
            print(f"\nComparison with {args.compare.name}:")
            print(f"  Keys in code: {len(found_keys)}")
            print(f"  Keys in JSON: {len(existing_keys)}")
            print(f"  Missing in JSON: {len(missing)}")
            print(f"  Extra in JSON: {len(extra)}")
            
            if missing:
                print(f"\n{'-'*60}")
                print("Missing keys (first 20):")
                print(f"{'-'*60}")
                for i, key in enumerate(sorted(missing)[:20], 1):
                    preview = key[:70] + '...' if len(key) > 70 else key
                    print(f"{i:2}. {preview}")
                    print(f"    Used in: {', '.join(key_locations[key][:2])}")
        
        except Exception as e:
            print(f"Error comparing: {e}")
    
    # 输出到文件
    if args.output:
        output_data = {}
        preserved_count = 0
        filtered_count = 0
        
        # 只包含代码中实际使用的 key
        for key in sorted(key_locations.keys()):
            output_data[key] = key  # 默认翻译为自己
        
        # 尝试保留现有翻译（只保留仍在使用的 key）
        if args.compare and args.compare.exists():
            try:
                with open(args.compare, 'r', encoding='utf-8') as f:
                    existing = json.load(f)
                
                for key in output_data:
                    if key in existing:
                        output_data[key] = existing[key]
                        preserved_count += 1
                
                # 计算被过滤掉的废弃 key
                existing_keys = set(existing.keys())
                used_keys = set(output_data.keys())
                filtered_keys = existing_keys - used_keys
                filtered_count = len(filtered_keys)
                
                if filtered_count > 0:
                    print(f"\n{'='*60}")
                    print(f"过滤结果：")
                    print(f"{'='*60}")
                    print(f"  原有翻译文件 key 数: {len(existing_keys)}")
                    print(f"  代码中实际使用的: {len(used_keys)}")
                    print(f"  保留的翻译: {preserved_count}")
                    print(f"  ✂️ 已过滤废弃 key: {filtered_count}")
                    
                    if filtered_count <= 20:
                        print(f"\n被过滤的废弃 key:")
                        for i, key in enumerate(sorted(filtered_keys), 1):
                            preview = key[:60] + '...' if len(key) > 60 else key
                            print(f"  {i:2}. {preview}")
                    else:
                        print(f"\n被过滤的废弃 key (前 20 个):")
                        for i, key in enumerate(sorted(filtered_keys)[:20], 1):
                            preview = key[:60] + '...' if len(key) > 60 else key
                            print(f"  {i:2}. {preview}")
                        print(f"  ... 还有 {filtered_count - 20} 个")
            except:
                pass
        
        with open(args.output, 'w', encoding='utf-8') as f:
            json.dump(output_data, f, indent=2, ensure_ascii=False)
            f.write('\n')
        
        print(f"\n[OK] 已导出到: {args.output}")
        print(f"[OK] 新文件包含 {len(output_data)} 个 key（已自动过滤废弃的 key）")
    
    return 0


if __name__ == '__main__':
    exit(main())
