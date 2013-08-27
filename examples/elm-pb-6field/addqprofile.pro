$ls data/cbm*.nc
name = ''
read, name, prompt='Please input grid file name:'
g = file_import(name)

q = fltarr(g.nx, g.ny)
for i=0, g.ny-1 do q(*, i) = - g.shiftangle / (2 * !PI)

handle = file_open(name, /write)
status = file_write(handle, 'q', q)
file_close, handle

g = file_import(name)
surface, g.q, chars=3

