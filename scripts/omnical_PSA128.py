#!/usr/bin/env python

import aipy as ap
import numpy as np
import commands, os, time, math, ephem, optparse, sys
import omnical.calibration_omni as omni
import cPickle as pickle
import scipy.signal as ss
import scipy.ndimage.filters as sfil
from scipy import interpolate
FILENAME = "omnical_PSA128.py"
print "#Omnical Version %s#"%omni.__version__
PI = np.pi
TPI = 2 * np.pi
######################################################################
##############Config parameters###################################
######################################################################
o = optparse.OptionParser()

ap.scripting.add_standard_options(o, cal=True, pol=True)
o.add_option('-t', '--tag', action = 'store', default = 'PSA128', help = 'tag name of this calibration')
o.add_option('-d', '--datatag', action = 'store', default = None, help = 'tag name of this data set')
o.add_option('-i', '--infopath', action = 'store', default = '/data2/home/hz2ug/omnical/doc/redundantinfo_PSA128_17ba.bin', help = 'redundantinfo file to read')
o.add_option('-r', '--rawcalpath', action = 'store', default = 'NORAWCAL', help = 'raw calibration parameter file to read. The file should be a pickle file generated by first_cal.py')
o.add_option('--add', action = 'store_true', help = 'whether to enable crosstalk removal')
o.add_option('--nadd', action = 'store', type = 'int', default = -1, help = 'time steps w to remove additive term with. for running average its 2w + 1 sliding window.')
o.add_option('--flagsigma', action = 'store', type = 'float', default = 4, help = 'Number of sigmas to flag on chi^2 distribution. 4 sigma by default.')
o.add_option('--flagt', action = 'store', type = 'int', default = 4, help = 'Number of time slices to run the minimum filter when flagging. 4 by default.')
o.add_option('--flagf', action = 'store', type = 'int', default = 4, help = 'Number of frequency slices to run the minimum filter when flagging. 4 by default.')
o.add_option('--datapath', action = 'store', default = 'NOBINDATA', help = 'binary data file folder to save/load binary data converted from uv file. Omit this option if you dont want to save binary data.')
o.add_option('--treasure', action = 'store', default = None, help = 'trasure folder to update.')
o.add_option('--healthbar', action = 'store', default = '2', help = 'health threshold (0-100) over which an antenna is marked bad.')
o.add_option('-o', '--outputpath', action = 'store', default = ".", help = 'output folder')
o.add_option('-k', '--skip', action = 'store_true', help = 'whether to skip data importing from uv')
o.add_option('-u', '--newuv', action = 'store_true', help = 'whether to create new uv files with calibration applied')
o.add_option('--flag', action = 'store_true', help = 'whether to create new flagging')
o.add_option('-f', '--overwrite', action = 'store_true', help = 'whether to overwrite if the new uv files already exists')
o.add_option('-s', '--singlethread', action = 'store_true', help = 'whether to disable multiprocessing for calibration and use only one thread. May need this option for things like grid engine.')
o.add_option('--chemo', action = 'store_true', help = 'whether to apply chemotherapy when flagging.')
o.add_option('--plot', action = 'store_true', help = 'whether to make plots in the end')
o.add_option('--mem', action = 'store', type = 'float', default = 4e9, help = 'Amount of initial memory to reserve when parsing uv files in number of bytes.')
o.add_option('--model_noise', action = 'store', default = None, help = 'A model .omnichisq file that contains the noise model (sigma^2) with the first two columns being lst in range [0,2pi). Separate by , the same order as -p. Need to be the same unit with data or model_treasure.')
o.add_option('--model_treasure', action = 'store', default = None, help = 'A treasure file that contains good foreground visibilities. ')

opts,args = o.parse_args(sys.argv[1:])
skip = opts.skip
create_new_uvs = opts.newuv
need_new_flag = opts.flag
overwrite_uvs = opts.overwrite
make_plots = opts.plot
ano = opts.tag##This is the file name difference for final calibration parameter result file. Result will be saved in miriadextract_xx_ano.omnical
dataano = opts.datatag#ano for existing data and lst.dat
sourcepath = os.path.expanduser(opts.datapath)
oppath = os.path.expanduser(opts.outputpath)
chemo = opts.chemo
if opts.treasure is not None:
    if not os.path.isdir(os.path.expanduser(opts.treasure)):
        raise IOError("Treasure path %s does not exist."%opts.treasure)
    treasurePath = os.path.expanduser(opts.treasure)
else:
    treasurePath = None
uvfiles = [os.path.expanduser(arg) for arg in args]
flag_thresh = opts.flagsigma
flagt = opts.flagt
flagf = opts.flagf
if opts.singlethread:
    nthread = 1
else:
    nthread = None

keep_binary_data = False
if os.path.isdir(sourcepath):
    keep_binary_data = True
elif opts.skip:
    raise IOError("Direct binary data import requested by -k or --skip option, but the --datapth %s doesn't exist."%sourcepath)

keep_binary_calpar = False

#print opts.healthbar, opts.healthbar.split(), len(opts.healthbar.split())
if len(opts.healthbar.split(',')) == 1:
    healthbar = float(opts.healthbar)
    ubl_healthbar = 100
elif len(opts.healthbar.split(',')) == 2:
    healthbar = float(opts.healthbar.split(',')[0])
    ubl_healthbar = float(opts.healthbar.split(',')[1])
else:
    raise Exception("User input healthbar option (--healthbar %s) is not recognized."%opts.healthbar)

init_mem = opts.mem

for uvf in uvfiles:
    if not os.path.isdir(uvf):
        uvfiles.remove(uvf)
        print "WARNING: uv file path %s does not exist!"%uvf
if len(uvfiles) == 0:
    raise Exception("ERROR: No valid uv files detected in input. Exiting!")

if dataano is None:
    dataano = ''
    for i, uvf in enumerate(uvfiles):
        if i!= 0:
            dataano = dataano + '_'
        while os.path.basename(uvf) == '' and len(uvf) > 0:
            uvf = uvf[:-1]
        dataano = dataano + os.path.basename(uvf)

wantpols = {}
for p in opts.pol.split(','):
    if len(p) != 2 or p[0] != p[1]:
        raise ValueError("polarization type %s not supported."%p)
    wantpols[p] = ap.miriad.str2pol[p]
#wantpols = {'xx':ap.miriad.str2pol['xx']}#, 'yy':-6}#todo:

have_model_noises = False
if opts.model_noise is not None:
    if not need_new_flag:
        raise IOError("--noise_model supplied without --flag. Noise model is only useful when doing new flagging.")
    if len(opts.model_noise.split(',')) != len(opts.pol.split(',')):
        raise ValueError("--model_noise got argument %s that does not have the same number of polarizations as -p argument %s."%(opts.model_noise, opts.pol))
    model_noises = {}
    for p, pol in enumerate(opts.pol.split(',')):
        model_noise_file = os.path.expanduser(opts.model_noise.split(',')[p])
        if not os.path.isfile(model_noise_file):
            raise IOError("model noise file %s does not exist"%model_noise_file)
        model_noises[pol] = omni.load_omnichisq(model_noise_file)
        if np.max(omni.get_omnitime(model_noises[pol])) >= TPI or np.min(omni.get_omnitime(model_noises[pol])) < 0:
            raise ValueError("Times stored in noise model %s is outside the range [0, 2pi)."%model_noise_file)
    have_model_noises = True

have_model_treasure = False
if opts.model_treasure is not None:
    if os.path.isdir(os.path.expanduser(opts.model_treasure)):
        model_treasure = omni.Treasure(os.path.expanduser(opts.model_treasure))
        for pol in opts.pol.split(','):
            if pol not in model_treasure.ubls.keys():
                raise ValueError("Polarization %s not found in the model treasure file %s."%(pol, os.path.expanduser(opts.model_treasure)))
    else:
        raise IOError("Model treasure folder not found: %s."%os.path.expanduser(opts.model_treasure))
    have_model_treasure = True

print "Reading calfile %s"%opts.cal,
sys.stdout.flush()
aa = ap.cal.get_aa(opts.cal, np.array([.15]))
print "Done"
sys.stdout.flush()

infopaths = {}
for pol in wantpols.keys():
    infopaths[pol]= os.path.expanduser(opts.infopath)


removedegen = 5
if opts.add and opts.nadd > 0:
    removeadditive = True
    removeadditiveperiod = opts.nadd
else:
    removeadditive = False
    removeadditiveperiod = -1

crudecalpath = os.path.expanduser(opts.rawcalpath)
needrawcal = False
if os.path.isfile(crudecalpath):
    needrawcal = True
    with open(crudecalpath, 'rb') as crude_calpar_file:
        crude_calpar = pickle.load(crude_calpar_file)
elif crudecalpath != 'NORAWCAL':
    raise IOError("Input rawcalpath %s doesn't exist on disk."%crudecalpath)


converge_percent = 0.001
max_iter = 20
step_size = .3

######################################################################
######################################################################
######################################################################

########Massage user parameters###################################
sourcepath += '/'
oppath += '/'
utcPath = sourcepath + 'miriadextract_' + dataano + "_localtime.dat"
lstPath = sourcepath + 'miriadextract_' + dataano + "_lsthour.dat"

####get some info from the first uvfile   ################
print "Getting some basic info from %s"%uvfiles[0],
sys.stdout.flush()
uv=ap.miriad.UV(uvfiles[0])
nfreq = uv.nchan;
nant = uv['nants']
sa = ephem.Observer()
sa.lon = aa.lon
sa.lat = aa.lat
#startfreq = uv['sfreq']
#dfreq = uv['sdf']
del(uv)
print "Done."
sys.stdout.flush()




###start reading miriads################
if skip:
    print FILENAME + " MSG: SKIPPED reading uvfiles. Reading binary data files directly...",
    sys.stdout.flush()
    with open(utcPath) as f:
        timing = [t.replace('\n','') for t in f.readlines()]
    with open(lstPath) as f:
        lst = [float(t) for t in f.readlines()]
    print (len(timing), nfreq, len(aa) * (len(aa) + 1) / 2), "...",
    data = np.array([np.fromfile(sourcepath + 'data_' + dataano + '_' + pol, dtype = 'complex64').reshape((len(timing), nfreq, len(aa) * (len(aa) + 1) / 2)) for pol in wantpols.keys()])
    rawflag = np.array([np.fromfile(sourcepath + 'flag_' + dataano + '_' + pol, dtype = 'bool').reshape((len(timing), nfreq, len(aa) * (len(aa) + 1) / 2)) for pol in wantpols.keys()])
    print "Done."
    sys.stdout.flush()

else:
    print FILENAME + " MSG:",  len(uvfiles), "uv files to be processed for " + ano
    sys.stdout.flush()
    data, t, timing, lst, rawflag = omni.importuvs(uvfiles, wantpols, totalVisibilityId = np.concatenate([[[i,j] for i in range(j + 1)] for j in range(len(aa))]), timingTolerance=100, init_mem=init_mem, lat = sa.lat, lon=sa.lon)#, nTotalAntenna = len(aa))
    print FILENAME + " MSG:",  len(t), "slices read. data shape: ", data.shape
    sys.stdout.flush()

    if keep_binary_data:
        print FILENAME + " MSG: saving binary data to disk...",
        sys.stdout.flush()
        f = open(utcPath,'w')
        for qaz in timing:
            f.write("%s\n"%qaz)
        f.close()
        f = open(lstPath,'w')
        for l in lst:
            f.write("%s\n"%l)
        f.close()
        for p,pol in zip(range(len(wantpols)), wantpols.keys()):
            data[p].tofile(sourcepath + 'data_' + dataano + '_' + pol)
            rawflag[p].tofile(sourcepath + 'flag_' + dataano + '_' + pol)
        print "Done."
        sys.stdout.flush()



#####print some astronomical info
sun = ephem.Sun()
sunpos  = np.zeros((len(timing), 2))
cenA = ephem.FixedBody()
cenA._ra = 3.5146
cenA._dec = -.75077
cenApos = np.zeros((len(timing), 2))
for nt,tm in zip(range(len(timing)),timing):
    sa.date = tm

    sun.compute(sa)
    sunpos[nt] = sun.alt, sun.az
    cenA.compute(sa)
    cenApos[nt] = cenA.alt, cenA.az
print FILENAME + " MSG: data time range UTC: %s to %s, sun altaz from (%f,%f) to (%f,%f)"%(timing[0], timing[-1], sunpos[0,0], sunpos[0,1], sunpos[-1,0], sunpos[-1,1])#, "CentaurusA altaz from (%f,%f) to (%f,%f)"%(cenApos[0,0], cenApos[0,1], cenApos[-1,0], cenApos[-1,1])
sys.stdout.flush()

###########initialize stuff
calibrators = {}
omnigains = {}
adds = {}
flags = {}
uvflags = {}
for p, pol in zip(range(len(data)), wantpols.keys()):
    ####create redundant calibrators################
    calibrators[pol] = omni.RedundantCalibrator_PAPER(aa)
    calibrators[pol].read_redundantinfo(infopaths[pol], verbose=False)
    info = calibrators[pol].Info.get_info()
    calibrators[pol].nTime = len(timing)
    calibrators[pol].nFrequency = nfreq

    ####consolidate 3D flags from uv files into per time/freq 2D flags
    uvflags[pol] = np.any(rawflag[p,:,:,calibrators[pol].subsetbl[calibrators[pol].crossindex]], axis = 0)# aweird transpose happens when slicing

    ###apply, if needed, raw calibration################
    if needrawcal:
        original_data = np.copy(data[p])
        data[p] = omni.apply_calpar(data[p], crude_calpar[pol], calibrators[pol].totalVisibilityId)

    ####calibrate################
    calibrators[pol].removeDegeneracy = removedegen
    calibrators[pol].convergePercent = converge_percent
    calibrators[pol].maxIteration = max_iter
    calibrators[pol].stepSize = step_size

    ################first round of calibration  #########################
    print FILENAME + " MSG: starting calibration on %s %s. nTime = %i, nFrequency = %i ..."%(dataano, pol, calibrators[pol].nTime, calibrators[pol].nFrequency),
    sys.stdout.flush()
    timer = time.time()
    additivein = np.zeros_like(data[p])

    calibrators[pol].logcal(data[p], additivein, nthread = nthread, verbose=True)

    if needrawcal:#restore original data's amplitude after logcal. dont restore phase because it may cause problem in degeneracy removal
        calibrators[pol].rawCalpar[:, :, 3:3 + calibrators[pol].nAntenna] = calibrators[pol].rawCalpar[:, :, 3:3 + calibrators[pol].nAntenna] + np.log10(np.abs(crude_calpar[pol][:, calibrators[pol].subsetant]))
        #calibrators[pol].rawCalpar[:, :, 3 + calibrators[pol].nAntenna:3 + 2 * calibrators[pol].nAntenna] = calibrators[pol].rawCalpar[:, :, 3 + calibrators[pol].nAntenna:3 + 2 * calibrators[pol].nAntenna] + np.angle(crude_calpar[pol][:, calibrators[pol].subsetant])
        #data[p] = np.copy(original_data)
        data[p] = omni.apply_calpar(data[p], 1/np.abs(crude_calpar[pol]), calibrators[pol].totalVisibilityId)
        #del original_data

    additiveout = calibrators[pol].lincal(data[p], additivein, nthread = nthread, verbose=True)
    #######################remove additive###############################
    if removeadditive:
        nadditiveloop = 1
        for i in range(nadditiveloop):
            #subtimer = omni.Timer()
            additivein[:,:,calibrators[pol].Info.subsetbl] = additivein[:,:,calibrators[pol].Info.subsetbl] + additiveout
            weight = ss.convolve(np.ones(additivein.shape[0]), np.ones(removeadditiveperiod * 2 + 1), mode='same')
            #for f in range(additivein.shape[1]):#doing for loop to save memory usage at the expense of negligible time
                #additivein[:,f] = ss.convolve(additivein[:,f], np.ones(removeadditiveperiod * 2 + 1)[:, None], mode='same')/weight[:, None]
            additivein = ((sfil.convolve1d(np.real(additivein), np.ones(removeadditiveperiod * 2 + 1), mode='constant') + 1j * sfil.convolve1d(np.imag(additivein), np.ones(removeadditiveperiod * 2 + 1), mode='constant'))/weight[:, None, None]).astype('complex64')
            calibrators[pol].computeUBLFit = False
            additiveout = calibrators[pol].lincal(data[p], additivein, nthread = nthread, verbose=True)

    if needrawcal:#restore original data's phase
        #calibrators[pol].rawCalpar[:, :, 3:3 + calibrators[pol].nAntenna] = calibrators[pol].rawCalpar[:, :, 3:3 + calibrators[pol].nAntenna] + np.log10(np.abs(crude_calpar[pol][:, calibrators[pol].subsetant]))
        calibrators[pol].rawCalpar[:, :, 3 + calibrators[pol].nAntenna:3 + 2 * calibrators[pol].nAntenna] = calibrators[pol].rawCalpar[:, :, 3 + calibrators[pol].nAntenna:3 + 2 * calibrators[pol].nAntenna] + np.angle(crude_calpar[pol][:, calibrators[pol].subsetant])
        #data[p] = np.copy(original_data)
        data[p] = omni.apply_calpar(data[p], np.exp(-1j * np.angle(crude_calpar[pol])), calibrators[pol].totalVisibilityId)
        additivein = omni.apply_calpar(additivein, np.exp(-1j * np.angle(crude_calpar[pol])), calibrators[pol].totalVisibilityId)
        #del original_data

    #omni.omniview([data[0,0,110], calibrators['xx'].get_calibrated_data(data[0])[0,110]], info = calibrators['xx'].get_info())
    if have_model_treasure and (sunpos[:,0] < -.1).all():
        #print np.nanmean(np.linalg.norm((data[0]-calibrators[pol].get_modeled_data())[..., calibrators[pol].subsetbl[calibrators[pol].crossindex]], axis = -1)**2 / calibrators[pol].rawCalpar[...,2])
        abs_cal, phs_cal = calibrators[pol].absolutecal_w_treasure(model_treasure, pol, np.array(lst)*TPI/24., tolerance = 2.)
        #print np.nanmean(np.linalg.norm((data[0]-calibrators[pol].get_modeled_data())[..., calibrators[pol].subsetbl[calibrators[pol].crossindex]], axis = -1)**2 / calibrators[pol].rawCalpar[...,2])
    #omni.omniview([data[0,0,110], calibrators['xx'].get_calibrated_data(data[0])[0,110]/5e4], info = calibrators['xx'].get_info())
    #####################flag bad data according to chisq#########################
    if need_new_flag:
        flags[pol] = calibrators[pol].flag(nsigma = flag_thresh, twindow=flagt, fwindow=flagf)#True if bad and flagged


        if have_model_noises and (sunpos[:,0] < -.1).all():
            if model_noises[pol][0,2] != calibrators[pol].nFrequency:
                raise ValueError('Model noise on %pol has nFrequency %i that differs from calibrators %i.'%(pol, model_noises[pol][0,2], calibrators[pol].nFrequency))
            interp_model = interpolate.interp1d(np.append(omni.get_omnitime(model_noises[pol]), [TPI]), np.append(model_noises[pol][..., 3:], [model_noises[pol][0, ..., 3:]], axis=0), axis = 0)
            model_chisq = 2 * interp_model(np.array(lst)*TPI/24.) * (len(calibrators[pol].crossindex) - calibrators[pol].nAntenna - calibrators[pol].nUBL + 2) * 10**(4 * np.median(calibrators[pol].rawCalpar[..., 3:3+calibrators[pol].nAntenna], axis = -1))#leniency factor * model * DOF * gain correction
            flags[pol] = flags[pol]| (calibrators[pol].rawCalpar[..., 2] > model_chisq)

        if chemo:
            flags[pol] = flags[pol]|(ss.convolve(flags[pol],[[.2,.4,1,.4,.2]],mode='same')>=1)
            flags[pol] = flags[pol]|(ss.convolve(flags[pol],[[.2],[.4],[1],[.4],[.2]],mode='same')>=1)
            bad_freq = (np.sum(flags[pol], axis = 0) > (calibrators[pol].nTime / 4.))
            bad_time = (np.sum(flags[pol][:, ~bad_freq], axis = 1) > (np.sum(~bad_freq) / 4.))
            flags[pol] = flags[pol]|bad_freq[None,:]|bad_time[:, None]
        flags[pol] = flags[pol] | uvflags[pol]
    else:
        flags[pol] = uvflags[pol]
    print "Done. %fmin"%(float(time.time()-timer)/60.)
    sys.stdout.flush()
    #######################save results###############################
    print FILENAME + " MSG: saving calibration results on %s %s."%(dataano, pol),
    sys.stdout.flush()
    calibrators[pol].utctimes = timing
    omnigains[pol[0]] = calibrators[pol].get_omnigain()
    adds[pol] = additivein


    if removeadditive:
        adds[pol].tofile(oppath + '/' + dataano + '_' + ano + "_%s.omniadd"%pol + str(removeadditiveperiod))
    if keep_binary_calpar:
        calibrators[pol].rawCalpar.tofile(oppath + '/' + dataano + '_' + ano + "_%s.omnical"%pol)
    else:
        calibrators[pol].get_omnichisq().tofile(oppath + '/' + dataano + '_' + ano + "_%s.omnichisq"%pol)
        calibrators[pol].get_omnifit().tofile(oppath + '/' + dataano + '_' + ano + "_%s.omnifit"%pol)
        omnigains[pol[0]].tofile(oppath + '/' + dataano + '_' + ano + "_%s.omnigain"%pol)
    flags[pol].tofile(oppath + '/' + dataano + '_' + ano + "_%s.omniflag"%pol)
    calibrators[pol].write_redundantinfo(oppath + '/' + dataano + '_' + ano + "_%s.binfo"%pol, overwrite=True)
    diag_txt = calibrators[pol].diagnose(data = data[p], additiveout = additiveout, healthbar = healthbar, ubl_healthbar = ubl_healthbar, ouput_txt = True)
    text_file = open(oppath + '/' + dataano + '_' + ano + "_%s.diagtxt"%pol, "a")
    text_file.write(str(sys.argv)+'\n'+diag_txt)
    text_file.close()

    print "Done."
    sys.stdout.flush()
    if treasurePath is not None and (sunpos[:,0] < -.1).all():
        print FILENAME + " MSG: updating treasure on %s %s in %s."%(dataano, pol, treasurePath),
        sys.stdout.flush()
        calibrators[pol].update_treasure(treasurePath, np.array(lst)*TPI/24., flags[pol], pol, verbose = True)
        print "Done."
        sys.stdout.flush()
if create_new_uvs:
    print FILENAME + " MSG: saving new uv files",
    sys.stdout.flush()
    infos = {}
    for pol in wantpols.keys():
        infos[pol[0]] = omni.read_redundantinfo(infopaths[pol])
    omni.apply_omnigain_uvs(uvfiles, omnigains, calibrators[wantpols.keys()[0]].totalVisibilityId, infos, oppath, ano, adds= adds, verbose = True, comment = '_'.join(sys.argv), flags = flags, overwrite = overwrite_uvs)
    print "Done."
    sys.stdout.flush()
if make_plots:
    import matplotlib.pyplot as plt
    for p,pol in zip(range(len(wantpols)), wantpols.keys()):
        #plt.subplot(2, len(wantpols), 2 * p + 1)
        #plot_data = (calibrators[pol].rawCalpar[:,:,2]/(len(calibrators[pol].Info.subsetbl)-calibrators[pol].Info.nAntenna - calibrators[pol].Info.nUBL))**.5
        #plt.imshow(plot_data, vmin = 0, vmax = (np.nanmax(calibrators[wantpols.keys()[0]].rawCalpar[:,:,2][~flags[wantpols.keys()[0]]])/(len(calibrators[pol].Info.subsetbl)-calibrators[pol].Info.nAntenna - calibrators[pol].Info.nUBL))**.5, interpolation='nearest')
        #plt.title('RMS fitting error per baseline')
        #plt.colorbar()

        plt.subplot(3, len(wantpols), 1 * len(wantpols) + p)
        flag_plot_data = (calibrators[pol].rawCalpar[:,:,2]/(len(calibrators[pol].Info.subsetbl)-calibrators[pol].Info.nAntenna - calibrators[pol].Info.nUBL))**.5
        vmax = np.percentile(flag_plot_data, 95)
        vmin = np.percentile(flag_plot_data, 5)
        flag_plot_data[flags[pol]] = np.nan
        plt.imshow(flag_plot_data, vmin = vmin, vmax = vmax, interpolation='nearest')
        plt.title('flagged RMS fitting error per baseline %s'%pol)
        plt.colorbar()

        plt.subplot(3, len(wantpols), 2 * len(wantpols) + p)
        shortest_ubli = np.argsort(np.linalg.norm(calibrators[pol].ubl, axis = 1))[0]
        shortest_vis = np.angle(calibrators[pol].rawCalpar[:,:,3+2*calibrators[pol].nAntenna+2*shortest_ubli] + 1.j * (calibrators[pol].rawCalpar[:,:,3+2*calibrators[pol].nAntenna+2*shortest_ubli+1]))
        #shortest_vis[flags[pol]] = np.nan
        plt.imshow(shortest_vis, interpolation='nearest')
        plt.title('phase of visibility fit on [%.1f, %.1f, %.1f] baseline %s'%(calibrators[pol].ubl[shortest_ubli][0], calibrators[pol].ubl[shortest_ubli][1], calibrators[pol].ubl[shortest_ubli][2], pol))
        plt.colorbar()

        plt.subplot(3, len(wantpols), 3 * len(wantpols) + p)
        shortest_vis_flag = np.copy(shortest_vis)
        shortest_vis_flag[flags[pol]] = np.nan
        plt.imshow(shortest_vis_flag, interpolation='nearest')
        plt.title('phase of visibility fit on [%.1f, %.1f, %.1f] baseline %s'%(calibrators[pol].ubl[shortest_ubli][0], calibrators[pol].ubl[shortest_ubli][1], calibrators[pol].ubl[shortest_ubli][2], pol))
        plt.colorbar()
    plt.show()
