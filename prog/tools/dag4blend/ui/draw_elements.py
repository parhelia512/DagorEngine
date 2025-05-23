#This file contains reusable blocks from drawing custom UI


# draws custom header, controlled by bool value or element of bool array
# control value is used for non-inicialized properties.
# control.name always exists; control[name] is None until user sets some value via owner.name
def draw_custom_header(layout, label,control_owner, control_name, control_value = None, index = None, icon = 'BLANK1'):
    header = layout.row(align = True)
    if control_value is None:
        control_value = control_owner[control_name]
    if index is None:
        header.prop(control_owner, control_name, text = "", emboss = False, expand = True,
                    icon = 'DOWNARROW_HLT' if control_value else 'RIGHTARROW_THIN')
        header.prop(control_owner, control_name, text = label, emboss = False, expand = True)
        header.prop(control_owner, control_name, text = "",    emboss = False, expand = True, icon = icon)
        return header
    header.prop(control_owner, control_name, text = "", emboss = False, expand = True, index = index,
                icon = 'DOWNARROW_HLT' if control_value else 'RIGHTARROW_THIN')
    header.prop(control_owner, control_name, text = label, index = index, emboss = False, expand = True)
    header.prop(control_owner, control_name, text = "",    index = index, emboss = False, expand = True, icon = icon)
    return header