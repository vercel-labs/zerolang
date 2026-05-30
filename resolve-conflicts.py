import re

with open('native/zero-c/src/main.c', 'r') as f:
    content = f.read()

lines = content.split('\n')

result = []
i = 0
while i < len(lines):
    line = lines[i]
    if line.strip().startswith('<<<<<<< HEAD'):
        head_lines = []
        i += 1
        while i < len(lines) and not lines[i].strip() == '=======':
            head_lines.append(lines[i])
            i += 1
        if i < len(lines) and lines[i].strip() == '=======':
            i += 1
        while i < len(lines) and not lines[i].strip().startswith('>>>>>>>'):
            i += 1
        if i < len(lines):
            i += 1
        result.extend(head_lines)
    else:
        result.append(line)
        i += 1

with open('native/zero-c/src/main.c', 'w') as f:
    f.write('\n'.join(result))

print('Done')
