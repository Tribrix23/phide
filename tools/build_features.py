import os
import sys

print("Applying CodeLens and Extensions updates...")

# ---------------------------------------------------------------------
# 1. Update include/phide/ui/ActivityRail.hpp
# ---------------------------------------------------------------------
path_rail_hpp = 'include/phide/ui/ActivityRail.hpp'
with open(path_rail_hpp, 'rb') as f:
    text = f.read().decode('utf-8')

old_enum = '''enum class RailTabId {
    None = -1,
    Files = 0,          ///< Files / Explorer perspective (two overlapping sheets)
    Search = 1,         ///< Search across workspace
    Git = 2,            ///< Source Control (Git)
    FileNetwork = 6,    ///< File Network icon with hub and 5 satellite nodes ("File network")
    MoreActions = 6,    ///< Backward compatibility alias for FileNetwork
    Packages = 3,       ///< Package Manager (PyCharm style)
    Notebook = 4,       ///< Interactive Notebook (Jupyter / Colab)
    Student = 5,        ///< Student Tutorials & Learning Hub (Student hat / graduation cap \U0001f393)
    Settings = 7        ///< Settings tab (Lucide gear icon, opens Settings editor tab)
};'''

new_enum = '''enum class RailTabId {
    None = -1,
    Files = 0,          ///< Files / Explorer perspective (two overlapping sheets)
    Search = 1,         ///< Search across workspace
    Git = 2,            ///< Source Control (Git)
    Extensions = 3,     ///< Extensions (VS Code marketplace)
    Packages = 4,       ///< Package Manager (PyCharm style)
    Notebook = 5,       ///< Interactive Notebook (Jupyter / Colab)
    Student = 6,        ///< Student Tutorials & Learning Hub
    FileNetwork = 7,    ///< File Network icon with hub and 5 satellite nodes
    MoreActions = 7,    ///< Backward compatibility alias for FileNetwork
    Settings = 8        ///< Settings tab
};'''

# If exact match fails due to unicode, do substring search
if 'enum class RailTabId {' in text:
    s_idx = text.find('enum class RailTabId {')
    e_idx = text.find('};', s_idx) + 2
    text = text[:s_idx] + new_enum + text[e_idx:]
    with open(path_rail_hpp, 'wb') as f:
        f.write(text.encode('utf-8'))
    print("Updated ActivityRail.hpp successfully!")

# ---------------------------------------------------------------------
# 2. Update src/ui/ActivityRail.cpp
# ---------------------------------------------------------------------
path_rail_cpp = 'src/ui/ActivityRail.cpp'
with open(path_rail_cpp, 'rb') as f:
    text = f.read().decode('utf-8')

target_items = '''    // 3. Git icon (directly under Search)
    m_items.push_back({ RailTabId::Git,      L"Source Control (Ctrl+Shift+G)", {} });'''

replace_items = '''    // 3. Git icon (directly under Search)
    m_items.push_back({ RailTabId::Git,        L"Source Control (Ctrl+Shift+G)", {} });

    // 4. Extensions icon (matching VS Code)
    m_items.push_back({ RailTabId::Extensions, L"Extensions (Ctrl+Shift+X)",     {} });'''

if target_items in text:
    text = text.replace(target_items, replace_items, 1)

target_switch = '''            case RailTabId::Git:
                m_lucide.DrawGitBranch(pRT, item.bounds, iconBrush, 1.6f);'''

replace_switch = '''            case RailTabId::Git:
                m_lucide.DrawGitBranch(pRT, item.bounds, iconBrush, 1.6f);
                break;
            case RailTabId::Extensions:
                m_lucide.DrawExtensionsGrid(pRT, item.bounds, iconBrush, 1.4f);'''

if target_switch in text:
    text = text.replace(target_switch, replace_switch, 1)

with open(path_rail_cpp, 'wb') as f:
    f.write(text.encode('utf-8'))
print("Updated ActivityRail.cpp successfully!")

print("Step 1 & 2 complete.")
