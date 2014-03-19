# Copyright (C) 2014  Peter Feiner

import collections
import os

from . import io

class Options(object):
    data_path = None

class Trie(object):
    def __init__(self):
        self.children = {}
        self.count = 0

    def add_path(self, values, reverse=False):
        if len(values) == 0:
            return
        if reverse:
            i = -1
        else:
            i = 0
        try:
            child = self.children[values[i]]
        except KeyError:
            child = Trie()
            self.children[values[i]] = child
        child.count += 1
        if reverse:
            child.add_path(values[:-1])
        else:
            child.add_path(values[1:])

    def child_count(self):
        return sum([child.count for child in self.children.values()])
        
def write_call_chains(out, root, prefix):
    total = root.child_count()
    sorted_frames = root.children.items()
    sorted_frames.sort(key=lambda x: -x[1].count)
    i = 0
    for frame, node in sorted_frames:
        i += 1
        percent = node.count * 100 / total
        if len(root.children) == 1:
            intro = '%s ' % prefix
        else:
            intro = '%s+-%2d%%' % (prefix[:-1], percent)
        #if not onlychild:
            #out.write('%s\n' % prefix)
        out.write('%s %s\n' % (intro, frame))
        if len(root.children) == 1:
            child_prefix = prefix[:-1] + ' '
        elif i == len(root.children):
            child_prefix = '%s   |' % (prefix[:-1])
        else:
            child_prefix = '%s  |' % prefix

        if node.children:
            code_prefix = child_prefix
        else:
            code_prefix = '%s ' % prefix[:-1]
        code = open(frame.filename).readlines()[frame.lineno - 1].strip()
        out.write('%s  >> %s\n' % (code_prefix, code))
        write_call_chains(out, node, child_prefix)

def write_call_chains(out, root, prefix):
    while len(root.children) == 1:
        frame, root = root.children.items()[0]
        code = open(frame.filename).readlines()[frame.lineno - 1].strip()
        out.write('%s%s\n' % (prefix, frame))
        out.write('%s>> %s\n' % (prefix, code))
        if root == None:
            return

    total = root.child_count()
    sorted_frames = root.children.items()
    sorted_frames.sort(key=lambda x: -x[1].count)
    i = 0
    for frame, node in sorted_frames:
        i += 1
        percent = node.count * 100 / total
        out.write('%s|\n' % prefix)
        out.write('%s|-%2d%% %s\n' % (prefix, percent, frame))
        if len(node.children) == 1:
            if i < len(root.children):
                child_prefix = prefix + '|     '
            else:
                child_prefix = prefix + '      '
            code_prefix = child_prefix
        else:
            child_prefix = prefix + '|   '
            code_prefix = child_prefix + '| '
        code = open(frame.filename).readlines()[frame.lineno - 1].strip()
        out.write('%s>> %s\n' % (code_prefix, code))
        write_call_chains(out, node, child_prefix)

def write(options, out):
    fp = open(options.data_path)
    call_chains = Trie()
    sample_count = 0
    for event in io.read_events(fp):
        if event.event_type == io.SAMPLE_EVENT: 
            sample_count += 1
            call_chains.add_path(event.data.frames, options.top_down)
    out.write('%d samples\n' % sample_count)
    write_call_chains(out, call_chains, '')
